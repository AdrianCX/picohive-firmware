/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2024 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include <string.h>

#include "analog_microphone_handler.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "pico_logger.h"

uint16_t __not_in_flash("adc") capture_buf1[CAPTURE_DEPTH] __attribute__((aligned(CAPTURE_DEPTH * sizeof(uint16_t))));
uint16_t __not_in_flash("adc") capture_buf2[CAPTURE_DEPTH] __attribute__((aligned(CAPTURE_DEPTH * sizeof(uint16_t))));

volatile uint32_t counter_dma1 = 0;
volatile uint32_t counter_dma2 = 0;

int dma_chan1 = 0;
int dma_chan2 = 0;

void dma_handler1()
{
    counter_dma1 = counter_dma1 + 1;
    dma_hw->ints0 = 1u << dma_chan1;
}

void dma_handler2()
{
    counter_dma2 = counter_dma2 + 1;
    dma_hw->ints0 = 1u << dma_chan2;
}

AnalogMicrophoneHandler& AnalogMicrophoneHandler::getInstance()
{
    static AnalogMicrophoneHandler instance;
    return instance;
}

void AnalogMicrophoneHandler::registerHandler(MicrophoneCallback *callback)
{
    m_callbacks.insert(callback);
}

bool AnalogMicrophoneHandler::start()
{
    if (m_active)
    {
        return true;
    }

    m_active = true;

    m_dma1 = counter_dma1;
    m_dma2 = counter_dma2;

    adc_gpio_init(ANALOG_MICROPHONE_GPIO);
    adc_init();
    adc_select_input(ANALOG_MICROPHONE_GPIO - 26);

    adc_fifo_setup(true, true, 1, false, false);

    float clk_div = (clock_get_hz(clk_adc) / (1.0 * SAMPLE_RATE)) - 1;
    adc_set_clkdiv(clk_div);

    adc_run(true);

    // Two DMAs chained to each other, so nothing has to be reconfigured on interrupt.
    dma_chan1 = dma_claim_unused_channel(true);
    dma_chan2 = dma_claim_unused_channel(true);

    {
        dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_chan2);

        channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&dma_channel_cfg, false);
        channel_config_set_write_increment(&dma_channel_cfg, true);

        channel_config_set_ring(&dma_channel_cfg, true, CAPTURE_RING_BITS);
        channel_config_set_dreq(&dma_channel_cfg, DREQ_ADC);

        channel_config_set_chain_to(&dma_channel_cfg, dma_chan1);

        dma_channel_set_irq1_enabled(dma_chan2, true);
        irq_set_exclusive_handler(DMA_IRQ_1, dma_handler2);
        irq_set_enabled(DMA_IRQ_1, true);

        dma_channel_configure(dma_chan2, &dma_channel_cfg, capture_buf2, &adc_hw->fifo, CAPTURE_DEPTH, false);
    }

    {
        dma_channel_config dma_channel_cfg = dma_channel_get_default_config(dma_chan1);

        channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&dma_channel_cfg, false);
        channel_config_set_write_increment(&dma_channel_cfg, true);

        channel_config_set_ring(&dma_channel_cfg, true, CAPTURE_RING_BITS);
        channel_config_set_dreq(&dma_channel_cfg, DREQ_ADC);

        channel_config_set_chain_to(&dma_channel_cfg, dma_chan2);

        irq_set_enabled(DMA_IRQ_0, true);
        irq_set_exclusive_handler(DMA_IRQ_0, dma_handler1);
        dma_channel_set_irq0_enabled(dma_chan1, true);

        dma_channel_configure(dma_chan1, &dma_channel_cfg, &capture_buf1[0], &adc_hw->fifo, CAPTURE_DEPTH, true);
    }

    return true;
}

void AnalogMicrophoneHandler::update()
{
    uint32_t last_dma1 = counter_dma1;
    if (last_dma1 != m_dma1)
    {
        m_dma1 = last_dma1;
        memcpy(&m_samples[0], &capture_buf1[0], CAPTURE_DEPTH * 2);
        reportBuffer();
    }

    uint32_t last_dma2 = counter_dma2;
    if (last_dma2 != m_dma2)
    {
        m_dma2 = last_dma2;
        memcpy(&m_samples[0], &capture_buf2[0], CAPTURE_DEPTH * 2);
        reportBuffer();
    }
}

void AnalogMicrophoneHandler::reportBuffer()
{
    for (int i = 0; i < CAPTURE_DEPTH; ++i)
    {
        m_samples[i] = m_samples[i] - m_bias;
    }

    for (MicrophoneCallback *callback : m_callbacks)
    {
        callback->audio_callback(&m_samples[0], CAPTURE_DEPTH);
    }
}
