/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"

#include "screen_state_machine_color.h"

ScreenStateMachineColor::ScreenStateMachineColor()
{
}

int ScreenStateMachineColor::onTimeout(int step, uint64_t now_us)
{
    if ((step == CONFIGURE) && (now_us - m_startUs < BUSY_TIMEOUT_US))
    {
        trace("ScreenStateMachineColor: not ready after reset, resetting again");
        return RESET_START;
    }

    return ScreenStateMachine::onTimeout(step, now_us);
}

int ScreenStateMachineColor::runStep(int step, uint64_t now_us)
{
    switch (step)
    {
        case RESET_START:
            trace("ScreenStateMachineColor: reset");
            setReset(true);
            waitDelay(now_us, 200000);
            return RESET_PULSE;

        case RESET_PULSE:
            setReset(false);
            waitDelay(now_us, 2000);
            return RESET_END;

        case RESET_END:
            setReset(true);
            waitDelay(now_us, 200000);
            return RESET_READY;

        case RESET_READY:
            waitBusy(true, now_us, READY_TIMEOUT_US);
            return CONFIGURE;

        case CONFIGURE:
            trace("ScreenStateMachineColor: configure");
            sendCommand(CMD_PANEL_SETTING, {0xEF, 0x08});
            sendCommand(CMD_POWER_SETTING, {0x37, 0x00, 0x23, 0x23});
            sendCommand(CMD_POWER_OFF_SEQUENCE, {0x00});
            sendCommand(CMD_BOOSTER_SOFT_START, {0xC7, 0xC7, 0x1D});
            sendCommand(CMD_PLL, {0x3C});
            sendCommand(CMD_TEMPERATURE_SENSOR, {0x00});
            sendCommand(CMD_VCOM_DATA_INTERVAL, {0x37});
            sendCommand(CMD_TCON, {0x22});
            sendCommand(CMD_RESOLUTION, {0x02, 0x58, 0x01, 0xC0});
            sendCommand(CMD_POWER_SAVING, {0xAA});
            waitDelay(now_us, 100000);
            return LOAD_IMAGE;

        case LOAD_IMAGE:
            trace("ScreenStateMachineColor: image data, m_imageSize=%u", (unsigned int)m_imageSize);
            sendCommand(CMD_VCOM_DATA_INTERVAL, {0x37});
            sendCommand(CMD_RESOLUTION, {0x02, 0x58, 0x01, 0xC0});
            sendCommand(CMD_DATA);
            sendData(m_image, m_imageSize);
            waitBusy(true, now_us);
            sendCommand(CMD_POWER_ON);
            return REFRESH;

        case REFRESH:
            trace("ScreenStateMachineColor: refresh");
            waitBusy(true, now_us);
            sendCommand(CMD_REFRESH);
            return POWER_OFF;

        case POWER_OFF:
            trace("ScreenStateMachineColor: power off");
            waitBusy(false, now_us);
            sendCommand(CMD_POWER_OFF);
            return COOL_DOWN;

        case COOL_DOWN:
            trace("ScreenStateMachineColor: refreshed, waiting for the panel to cool down");
            waitDelay(now_us, 4000000);
            return DEEP_SLEEP;

        case DEEP_SLEEP:
            sendCommand(CMD_DEEP_SLEEP, {0xA5});
            waitDelay(now_us, 100000);
            return RESET_HOLD;

        case RESET_HOLD:
            setReset(false);
            trace("ScreenStateMachineColor: panel asleep");
            return DONE;
    }

    return DONE;
}
