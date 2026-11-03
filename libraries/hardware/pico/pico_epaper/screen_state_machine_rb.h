/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "screen_state_machine.h"

class ScreenStateMachineRb : public ScreenStateMachine
{
public:
    ScreenStateMachineRb();

protected:
    int runStep(int step, uint64_t now_us) override;
    void requestStatus() override;

private:
    static const uint64_t STATUS_POLL_US = 200000;

    enum Step
    {
        RESET_START,
        RESET_PULSE,
        RESET_END,
        POWER_ON,
        POWER_ON_SETTLE,
        LOAD_IMAGE,
        POWER_OFF,
        COOL_DOWN,
        SLEEP_POWER_OFF,
        SLEEP_SETTLE,
        DEEP_SLEEP,
    };

    enum Command : uint8_t
    {
        CMD_PANEL_SETTING = 0x00,
        CMD_POWER_SETTING = 0x01,
        CMD_POWER_OFF = 0x02,
        CMD_POWER_ON = 0x04,
        CMD_DEEP_SLEEP = 0x07,
        CMD_DATA_BLACK = 0x10,
        CMD_REFRESH = 0x12,
        CMD_DATA_RED = 0x13,
        CMD_DUAL_SPI = 0x15,
        CMD_VCOM_DATA_INTERVAL = 0x50,
        CMD_TCON = 0x60,
        CMD_RESOLUTION = 0x61,
        CMD_GET_STATUS = 0x71,
    };
};
