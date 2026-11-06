/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// No microphone on linux: no block ever arrives.

#include "analog_microphone_handler.h"
#include "pico_logger.h"

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
    if (!m_active)
    {
        trace("AnalogMicrophoneHandler::start: mock, no audio will arrive\n");
    }
    m_active = true;
    return true;
}

void AnalogMicrophoneHandler::update()
{
}
