/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"

#include "screen_state_machine_rb.h"

ScreenStateMachineRb::ScreenStateMachineRb()
{
    m_busyPollUs = STATUS_POLL_US;
}

void ScreenStateMachineRb::requestStatus()
{
    sendCommand(CMD_GET_STATUS);
}

int ScreenStateMachineRb::runStep(int step, uint64_t now_us)
{
    switch (step)
    {
        case RESET_START:
            trace("ScreenStateMachineRb: reset");
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
            return POWER_ON;

        case POWER_ON:
            trace("ScreenStateMachineRb: power on");
            sendCommand(CMD_POWER_SETTING, {0x07, 0x07, 0x3F, 0x3F});
            waitBusy(true, now_us);
            sendCommand(CMD_POWER_ON);
            return POWER_ON_SETTLE;

        case POWER_ON_SETTLE:
            waitDelay(now_us, 200000);
            return LOAD_IMAGE;

        case LOAD_IMAGE:
            trace("ScreenStateMachineRb: image data and refresh, m_imageSize=%u", (unsigned int)m_imageSize);
            sendCommand(CMD_PANEL_SETTING, {0x0F});
            sendCommand(CMD_RESOLUTION, {0x02, 0x88, 0x01, 0xE0});
            sendCommand(CMD_DUAL_SPI, {0x00});
            sendCommand(CMD_VCOM_DATA_INTERVAL, {0x11, 0x07});
            sendCommand(CMD_TCON, {0x22});

            sendCommand(CMD_DATA_BLACK);
            sendData(m_image, m_imageSize / 2);
            sendCommand(CMD_DATA_RED);
            sendData(m_image + m_imageSize / 2, m_imageSize / 2);

            waitBusy(true, now_us);
            sendCommand(CMD_REFRESH);
            return POWER_OFF;

        case POWER_OFF:
            trace("ScreenStateMachineRb: refreshed, power off");
            waitBusy(true, now_us);
            sendCommand(CMD_POWER_OFF);
            return COOL_DOWN;

        case COOL_DOWN:
            trace("ScreenStateMachineRb: waiting for the panel to cool down");
            waitDelay(now_us, 4000000);
            return SLEEP_POWER_OFF;

        case SLEEP_POWER_OFF:
            waitBusy(true, now_us);
            sendCommand(CMD_POWER_OFF);
            return SLEEP_SETTLE;

        case SLEEP_SETTLE:
            waitDelay(now_us, 200000);
            return DEEP_SLEEP;

        case DEEP_SLEEP:
            sendCommand(CMD_DEEP_SLEEP, {0xA5});
            trace("ScreenStateMachineRb: panel asleep");
            return DONE;
    }

    return DONE;
}
