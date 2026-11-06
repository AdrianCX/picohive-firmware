/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2024 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>

#include "general_config.h"

#ifndef CAPTURE_DEPTH
#define CAPTURE_DEPTH 1024
#endif

static_assert((CAPTURE_DEPTH >= 64) && ((CAPTURE_DEPTH & (CAPTURE_DEPTH - 1)) == 0), "CAPTURE_DEPTH has to be a power of two, 64 or more");

constexpr uint32_t capture_ring_bits()
{
    uint32_t bits = 0;

    while ((1u << bits) < (CAPTURE_DEPTH * sizeof(uint16_t)))
    {
        ++bits;
    }

    return bits;
}

#define CAPTURE_RING_BITS capture_ring_bits()

class MicrophoneCallback
{
public:
    virtual void audio_callback(const int16_t *samples, size_t size) = 0;
};

// ADC on ANALOG_MICROPHONE_GPIO at SAMPLE_RATE (general_config.h), into two chained DMA buffers.
class AnalogMicrophoneHandler
{
public:
    static AnalogMicrophoneHandler& getInstance();

    void registerHandler(MicrophoneCallback *callback);
    bool start();
    void update();

private:
    void reportBuffer();

    bool m_active = false;
    std::set<MicrophoneCallback *> m_callbacks;
    int16_t m_samples[CAPTURE_DEPTH] = {0};

    uint32_t m_dma1 = 0;
    uint32_t m_dma2 = 0;

    static constexpr float m_biasVoltage = 1.25;
    static constexpr int16_t m_bias = ((int16_t)((m_biasVoltage * 4095) / 3.3));
};
