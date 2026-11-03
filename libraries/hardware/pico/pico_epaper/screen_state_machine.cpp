/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico_logger.h"
#include "general_config.h"

#include "screen_state_machine.h"

volatile uint32_t ScreenStateMachine::m_busyHighCount = 0;
volatile uint32_t ScreenStateMachine::m_busyLowCount = 0;

ScreenStateMachine *ScreenStateMachine::m_instance = NULL;

ScreenStateMachine *ScreenStateMachine::get()
{
    return m_instance;
}

void ScreenStateMachine::onBusyIrq()
{
    uint32_t events = gpio_get_irq_event_mask(GP_EPD_BUSY_PIN);

    if (events == 0)
    {
        return;
    }

    gpio_acknowledge_irq(GP_EPD_BUSY_PIN, events);

    if (events & GPIO_IRQ_EDGE_FALL)
    {
        m_busyLowCount = m_busyLowCount + 1;
    }

    if (events & GPIO_IRQ_EDGE_RISE)
    {
        m_busyHighCount = m_busyHighCount + 1;
    }
}

ScreenStateMachine::ScreenStateMachine()
{
    gpio_add_raw_irq_handler(GP_EPD_BUSY_PIN, &ScreenStateMachine::onBusyIrq);
    gpio_set_irq_enabled(GP_EPD_BUSY_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);

    m_instance = this;
}

bool ScreenStateMachine::show_image(const uint8_t *image, size_t size)
{
    if (m_step != DONE)
    {
        return false;
    }

    m_image = image;
    m_imageSize = size;
    m_wait = WAIT_NONE;
    m_timedOut = false;
    m_failed = false;
    m_startUs = time_us_64();
    m_startBusyHighCount = m_busyHighCount;
    m_startBusyLowCount = m_busyLowCount;
    m_step = 0;

    trace("ScreenStateMachine::show_image: start, size=%u pin=%d edges high[%u] low[%u]", (unsigned int)size, (int)gpio_get(GP_EPD_BUSY_PIN), (unsigned int)m_busyHighCount, (unsigned int)m_busyLowCount);
    return true;
}

void ScreenStateMachine::update(uint64_t now_us)
{
    if ((m_step == DONE) || !waitOver(now_us))
    {
        return;
    }

    m_wait = WAIT_NONE;

    if (m_timedOut)
    {
        m_timedOut = false;
        m_step = onTimeout(m_step, now_us);
    }
    else
    {
        m_step = runStep(m_step, now_us);
    }

    if (m_step == DONE)
    {
        checkPanelResponded();
        trace("ScreenStateMachine::update: refresh done after %u ms, failed=%d", (unsigned int)((now_us - m_startUs) / 1000), (int)m_failed);
    }

    if ((m_step == DONE) && m_sleepOnShow)
    {
        trace("ScreenStateMachine::update: panel asleep, going to sleep");
        general_sleep();
    }
}

void ScreenStateMachine::checkPanelResponded()
{
    if (m_failed || (m_busyHighCount != m_startBusyHighCount) || (m_busyLowCount != m_startBusyLowCount))
    {
        return;
    }

    trace("ScreenStateMachine: HARDWARE PROBLEM: panel never toggled busy pin - check panel ribbon cable", GP_EPD_BUSY_PIN, (int)gpio_get(GP_EPD_BUSY_PIN));
    m_failed = true;
}

int ScreenStateMachine::onTimeout(int step, uint64_t now_us)
{
    trace("ScreenStateMachine::onTimeout: HARDWARE PROBLEM: panel stayed busy for %u s before step %d, - check panel ribbon cable", (unsigned int)(BUSY_TIMEOUT_US / 1000000), step);
    m_failed = true;
    return DONE;
}

void ScreenStateMachine::waitDelay(uint64_t now_us, uint64_t delay_us)
{
    m_wait = WAIT_DELAY;
    m_waitStartUs = now_us;
    m_waitUs = delay_us;
}

void ScreenStateMachine::waitBusy(bool level, uint64_t now_us, uint64_t timeout_us)
{
    m_wait = level ? WAIT_BUSY_HIGH : WAIT_BUSY_LOW;
    m_waitStartUs = now_us;
    m_waitUs = timeout_us;
    m_lastPollUs = now_us;
    m_timedOut = false;
    m_lastBusyHighCount = m_busyHighCount;
    m_lastBusyLowCount = m_busyLowCount;
}

bool ScreenStateMachine::waitOver(uint64_t now_us)
{
    if (m_wait == WAIT_NONE)
    {
        return true;
    }

    uint64_t waited = now_us - m_waitStartUs;

    if (m_wait == WAIT_DELAY)
    {
        return waited >= m_waitUs;
    }

    bool high = (m_wait == WAIT_BUSY_HIGH);

    if (high ? (m_busyHighCount != m_lastBusyHighCount) : (m_busyLowCount != m_lastBusyLowCount))
    {
        logBusyWaitOver("irq edge", high, waited);
        return true;
    }

    if ((m_waitUs != 0) && (waited >= m_waitUs))
    {
        logBusyWaitOver("timeout", high, waited);
        m_timedOut = true;
        return true;
    }

    if (now_us - m_lastPollUs < m_busyPollUs)
    {
        return false;
    }

    m_lastPollUs = now_us;
    requestStatus();

    if (gpio_get(GP_EPD_BUSY_PIN) != (high ? 1 : 0))
    {
        return false;
    }

    logBusyWaitOver("pin poll", high, waited);
    return true;
}

void ScreenStateMachine::logBusyWaitOver(const char *how, bool high, uint64_t waited_us)
{
    trace("ScreenStateMachine::waitOver: step=%d busy %s wait over by %s after %u ms, pin=%d edges high[%u->%u] low[%u->%u]",
          m_step, high ? "high" : "low", how, (unsigned int)(waited_us / 1000), (int)gpio_get(GP_EPD_BUSY_PIN),
          (unsigned int)m_lastBusyHighCount, (unsigned int)m_busyHighCount, (unsigned int)m_lastBusyLowCount, (unsigned int)m_busyLowCount);
}

void ScreenStateMachine::setReset(bool level)
{
    gpio_put(GP_EPD_RST_PIN, level ? 1 : 0);
}

void ScreenStateMachine::sendCommand(uint8_t command, std::initializer_list<uint8_t> data)
{
    gpio_put(GP_EPD_DC_PIN, 0);
    gpio_put(GP_EPD_CS_PIN, 0);
    spi_write_blocking(DC_SPI_PORT, &command, 1);
    gpio_put(GP_EPD_CS_PIN, 1);

    if (data.size() != 0)
    {
        sendData(data.begin(), data.size());
    }
}

void ScreenStateMachine::sendData(const uint8_t *data, size_t size)
{
    gpio_put(GP_EPD_DC_PIN, 1);
    gpio_put(GP_EPD_CS_PIN, 0);
    spi_write_blocking(DC_SPI_PORT, data, size);
    gpio_put(GP_EPD_CS_PIN, 1);
}
