/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "screen_state_machine.h"

class ScreenStateMachineColor : public ScreenStateMachine
{
public:
    ScreenStateMachineColor();

protected:
    int runStep(int step, uint64_t now_us) override;
    int onTimeout(int step, uint64_t now_us) override;

private:
    static const uint64_t READY_TIMEOUT_US = 1000000;

    enum Step
    {
        RESET_START,
        RESET_PULSE,
        RESET_END,
        RESET_READY,
        CONFIGURE,
        LOAD_IMAGE,
        REFRESH,
        POWER_OFF,
        COOL_DOWN,
        DEEP_SLEEP,
        RESET_HOLD,
    };

    enum Command : uint8_t
    {
        CMD_PANEL_SETTING = 0x00,
        CMD_POWER_SETTING = 0x01,
        CMD_POWER_OFF = 0x02,
        CMD_POWER_OFF_SEQUENCE = 0x03,
        CMD_POWER_ON = 0x04,
        CMD_BOOSTER_SOFT_START = 0x06,
        CMD_DEEP_SLEEP = 0x07,
        CMD_DATA = 0x10,
        CMD_REFRESH = 0x12,
        CMD_PLL = 0x30,
        CMD_TEMPERATURE_SENSOR = 0x41,
        CMD_VCOM_DATA_INTERVAL = 0x50,
        CMD_TCON = 0x60,
        CMD_RESOLUTION = 0x61,
        CMD_POWER_SAVING = 0xE3,
    };
};
