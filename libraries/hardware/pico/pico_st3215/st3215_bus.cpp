/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "pico/stdlib.h"
#include "st3215.h"
#include "pico_helper.h"
#include "pico_logger.h"

bool ST3215Bus::begin(const ST3215BusConfig& config)
{
    m_config = config;

    uint8_t index = uart_get_index(config.uart);
    if ((config.tx_gpio % 4 != 0) || (config.rx_gpio != config.tx_gpio + 1) || ((((config.tx_gpio + 4) & 8) ? 1 : 0) != index))
    {
        trace("st3215: GP%d/GP%d are not tx/rx of uart%d; uart0 is GP0/1, GP12/13, GP16/17, uart1 GP4/5, GP8/9, GP20/21", config.tx_gpio, config.rx_gpio, index);
        return false;
    }

    uint32_t actual = uart_init(config.uart, config.baudrate);
    uart_set_format(config.uart, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(config.uart, false, false);
    uart_set_fifo_enabled(config.uart, true);

    gpio_set_function(config.tx_gpio, UART_FUNCSEL_NUM(config.uart, config.tx_gpio));
    gpio_set_function(config.rx_gpio, UART_FUNCSEL_NUM(config.uart, config.rx_gpio));
    gpio_pull_up(config.rx_gpio);

    gpio_set_drive_strength(config.tx_gpio, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(config.tx_gpio, GPIO_SLEW_RATE_FAST);
    gpio_set_input_hysteresis_enabled(config.rx_gpio, true);

    trace("st3215: uart%d tx=GP%d rx=GP%d at %u baud (asked %u), tx 12mA fast slew and %s, rx pulled up with hysteresis, %ums timeout, %u attempts",
          index, config.tx_gpio, config.rx_gpio, (unsigned)actual, (unsigned)config.baudrate,
          config.release_tx ? "released between packets" : "held by the uart throughout",
          (unsigned)config.timeout_ms, (unsigned)config.attempts);

    set_quiet_window();
    release_tx();

    m_started = true;
    return true;
}

static const uint32_t ST3215_QUIET_FLOOR_US = 250;
static const uint32_t ST3215_TRACE_BYTES = 12;
static const uint64_t ST3215_REPORT_US = 60ull * 1000 * 1000;

void ST3215Bus::drive_tx()
{
    if (m_config.release_tx)
    {
        gpio_set_function(m_config.tx_gpio, UART_FUNCSEL_NUM(m_config.uart, m_config.tx_gpio));
    }
}

void ST3215Bus::release_tx()
{
    if (!m_config.release_tx)
    {
        return;
    }

    gpio_set_function(m_config.tx_gpio, GPIO_FUNC_SIO);
    gpio_set_pulls(m_config.tx_gpio, false, false);
    gpio_set_dir(m_config.tx_gpio, GPIO_IN);
}

bool ST3215Bus::line_test(ST3215Line& line)
{
    if (!m_started)
    {
        return false;
    }

    static const uint32_t SETTLE_US = 2000;

    line.rx_idle_high = (gpio_get(m_config.rx_gpio) != 0);

    gpio_set_function(m_config.tx_gpio, GPIO_FUNC_SIO);
    gpio_set_dir(m_config.tx_gpio, GPIO_OUT);

    gpio_put(m_config.tx_gpio, 0);
    busy_wait_us(SETTLE_US);
    line.pulled_low = (gpio_get(m_config.rx_gpio) == 0);

    gpio_put(m_config.tx_gpio, 1);
    busy_wait_us(SETTLE_US);
    line.released_high = (gpio_get(m_config.rx_gpio) != 0);

    drive_tx();
    release_tx();
    drain_line(ST3215_BROADCAST_ID, "after driving the line by hand", false);

    trace("st3215: line test: idle %s, driven low reads %s, let go reads %s",
          line.rx_idle_high ? "high" : "LOW",
          line.pulled_low ? "low" : "HIGH - the line will not go down",
          line.released_high ? "high" : "LOW - something is holding the line down");

    if (!line.pulled_low)
    {
        trace("st3215: line test: the pico cannot pull the bus low through the series resistor; each servo "
              "adds a pull-up, so it gets worse with more servos. Use a 220-470R resistor or a half duplex driver");
    }

    return true;
}

bool ST3215Bus::set_baudrate(uint32_t baudrate)
{
    if (!m_started)
    {
        return false;
    }

    uint32_t actual = uart_set_baudrate(m_config.uart, baudrate);
    m_config.baudrate = baudrate;
    set_quiet_window();

    // Whatever was mid flight at the old rate is meaningless at the new one.
    drain_line(ST3215_BROADCAST_ID, "changing baudrate", false);

    trace("st3215: bus now speaking %u baud (asked %u)", (unsigned int)actual, (unsigned int)baudrate);
    return true;
}

bool ST3215Bus::failed(const char *what, uint8_t id)
{
    if (m_quiet)
    {
        return false;
    }

    ++m_failures;

    uint64_t now = time_us_64();
    if ((m_failed_said_us != 0) && ((now - m_failed_said_us) < ST3215_REPORT_US))
    {
        return false;
    }

    uint32_t hidden = m_failures - m_failures_said - 1;

    if (hidden > 0)
    {
        trace("st3215: id %d: %s (and %u other packet%s got no answer in the last minute)",
              id, what, (unsigned int)hidden, (hidden == 1) ? "" : "s");
    }
    else
    {
        trace("st3215: id %d: %s", id, what);
    }

    m_failed_said_us = now;
    m_failures_said = m_failures;
    return false;
}

bool ST3215Bus::probe(uint8_t id)
{
    m_quiet = true;
    bool answered = ping(id);
    m_quiet = false;
    return answered;
}

uint32_t ST3215Bus::drain_line(uint8_t id, const char *why, bool settle)
{
    if (!settle && !uart_is_readable(m_config.uart))
    {
        return 0;
    }

    uint8_t seen[ST3215_TRACE_BYTES];
    uint32_t dropped = 0;
    uint64_t give_up = time_us_64() + ((uint64_t)m_config.timeout_ms * 1000);

    while (uart_is_readable_within_us(m_config.uart, m_quiet_us))
    {
        uint8_t byte = (uint8_t)uart_getc(m_config.uart);

        if (dropped < ST3215_TRACE_BYTES)
        {
            seen[dropped] = byte;
        }

        ++dropped;

        if (time_us_64() >= give_up)
        {
            trace("st3215: id %d: %s - line still busy after %ums, stopped draining at %u bytes",
                  id, why, (unsigned)m_config.timeout_ms, (unsigned)dropped);
            break;
        }
    }

    if (dropped > 0)
    {
        char hex[(ST3215_TRACE_BYTES * 3) + 1];
        PicoHelper::hex_into(hex, sizeof(hex), seen, (dropped < ST3215_TRACE_BYTES) ? dropped : ST3215_TRACE_BYTES);

        trace("st3215: id %d: %s - dropped %u byte%s off the line [%s%s]",
              id, why, (unsigned)dropped, (dropped == 1) ? "" : "s", hex,
              (dropped > ST3215_TRACE_BYTES) ? " ..." : "");

        m_last_noise = true;
    }

    return dropped;
}

void ST3215Bus::set_quiet_window()
{
    uint32_t byte_us = (m_config.baudrate > 0) ? ((10 * 1000000) / m_config.baudrate) : 100;
    uint32_t want = byte_us * 4;

    m_quiet_us = (want > ST3215_QUIET_FLOOR_US) ? want : ST3215_QUIET_FLOOR_US;
}

uint32_t ST3215Bus::take_line_errors()
{
    // FE, PE, BE, OE - framing, parity, break, overrun. Writing anything clears them.
    uint32_t bits = uart_get_hw(m_config.uart)->rsr & 0x0F;

    if (bits != 0)
    {
        uart_get_hw(m_config.uart)->rsr = 0x0F;
        ++m_line_errors;
    }

    return bits;
}

void ST3215Bus::note_echo_miss(uint8_t id, uint32_t skipped, uint32_t line_errors)
{
    ++m_echo_misses;

    uint64_t now = time_us_64();

    m_echo_said_us = now;
    m_echo_misses_said = m_echo_misses;
}

ST3215Bus::Heard ST3215Bus::find_status(uint8_t id, const uint8_t *sent, size_t sent_len, uint8_t *status,
                                        uint8_t reply_count, uint64_t deadline_us, uint32_t& skipped, bool& echo_intact)
{
    ST3215Finder finder;
    size_t heard = 0;

    while (true)
    {
        uint64_t now = time_us_64();

        if ((now >= deadline_us) || !uart_is_readable_within_us(m_config.uart, (uint32_t)(deadline_us - now)))
        {
            skipped = finder.skipped;
            echo_intact = finder.echo_intact;

            return (heard > sent_len) ? Heard::Rubbish : Heard::Silence;
        }

        ++heard;

        if (st3215_finder_feed(finder, (uint8_t)uart_getc(m_config.uart), id, reply_count, sent, sent_len) == ST3215Found::Answer)
        {
            memcpy(status, finder.window, (size_t)finder.window[3] + 4);
            skipped = finder.skipped;
            echo_intact = finder.echo_intact;
            return Heard::Answer;
        }
    }
}

bool ST3215Bus::transact(uint8_t id, uint8_t instruction, const uint8_t *params, uint8_t count, uint8_t *reply, uint8_t reply_count)
{
    if (!m_started)
    {
        return failed("bus not started, call begin()", id);
    }

    uint8_t packet[ST3215_MAX_PACKET];
    size_t len = st3215_build_packet(id, instruction, params, count, packet, sizeof(packet));
    if (len == 0)
    {
        return failed("packet too large", id);
    }

    bool framing = false;

    for (uint32_t attempt = 1; attempt <= ((m_config.attempts > 0) ? m_config.attempts : 1); ++attempt)
    {
        drain_line(id, "before sending", false);
        m_last_noise = false;
        take_line_errors();

        drive_tx();
        uart_write_blocking(m_config.uart, packet, len);
        uart_tx_wait_blocking(m_config.uart);

        release_tx();

        uint64_t deadline = time_us_64() + ((uint64_t)m_config.timeout_ms * 1000);

        if (id == ST3215_BROADCAST_ID)
        {
            drain_line(id, "broadcast, no answer expected", true);
            return true;
        }

        uint8_t status[ST3215_MAX_PACKET];
        uint32_t skipped = 0;
        bool echo_intact = false;
        Heard heard = find_status(id, packet, len, status, reply_count, deadline, skipped, echo_intact);
        uint32_t line_errors = take_line_errors();

        if (heard == Heard::Silence)
        {
            char what[128];
            snprintf(what, sizeof(what),
                     "nothing answers on this id at %u baud: the plug, the 12V, the shared ground - or it is "
                     "set to another baudrate, which scanbaud will find",
                     (unsigned int)m_config.baudrate);

            return failed(what, id);
        }

        if (heard == Heard::Answer)
        {
            if (!echo_intact)
            {
                note_echo_miss(id, skipped, line_errors);
            }
            else if (skipped > 0)
            {
                trace("st3215: id %d: answered after %u byte%s that were neither the echo nor the answer",
                      id, (unsigned int)skipped, (skipped == 1) ? "" : "s");
                m_last_noise = true;
            }

            if (uart_is_readable(m_config.uart))
            {
                uint32_t extra = drain_line(id, "extra bytes after a good answer", false);
                trace("st3215: id %d: %u bytes arrived after the answer - two servos are probably both id %d",
                      id, (unsigned int)extra, id);
            }

            uint64_t now_us = time_us_64();

            ST3215Status parsed;
            st3215_parse_status(status, (size_t)status[3] + 4, parsed);

            if (parsed.error != m_id_error[id])
            {
                char text[64];
                trace("st3215: id %d: %s", id, parsed.error ? st3215_error_text(parsed.error, text, sizeof(text)) : "errors cleared");
            }

            m_last_error = parsed.error;
            m_id_error[id] = parsed.error;

            if ((m_failures > m_failures_said) && ((now_us - m_failed_said_us) >= ST3215_REPORT_US))
            {
                trace("st3215: id %d: answering again, after %u packet%s that got no answer",
                      id, (unsigned int)(m_failures - m_failures_said),
                      ((m_failures - m_failures_said) == 1) ? "" : "s");

                m_failed_said_us = now_us;
                m_failures_said = m_failures;
            }

            if (reply_count > 0)
            {
                memcpy(reply, parsed.params, reply_count);
            }

            return true;
        }

        m_last_noise = true;
        framing = framing || ((line_errors & 0x0F) != 0);
        drain_line(id, "after an answer that was not one", true);
    }

    return failed(framing ? "garbled reply, with uart framing errors - or two servos on this id" : "garbled reply - or two servos on this id", id);
}

bool ST3215Bus::ping(uint8_t id)
{
    return transact(id, ST3215_PING, NULL, 0, NULL, 0);
}

bool ST3215Bus::read(uint8_t id, uint8_t reg, uint8_t *data, uint8_t count)
{
    uint8_t params[2] = { reg, count };
    return transact(id, ST3215_READ, params, sizeof(params), data, count);
}

bool ST3215Bus::write(uint8_t id, uint8_t reg, const uint8_t *data, uint8_t count)
{
    if (count >= ST3215_MAX_PARAMS)
    {
        return failed("write too large", id);
    }

    uint8_t params[ST3215_MAX_PARAMS];
    params[0] = reg;
    memcpy(params + 1, data, count);
    return transact(id, ST3215_WRITE, params, count + 1, NULL, 0);
}
