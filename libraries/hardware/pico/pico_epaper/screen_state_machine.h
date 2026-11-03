/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <initializer_list>

class ScreenStateMachine
{
public:
    ScreenStateMachine();

    static ScreenStateMachine *get();

    bool show_image(const uint8_t *image, size_t size);
    void update(uint64_t now_us);
    bool busy() const { return m_step != DONE; }
    bool failed() const { return m_failed; }

    void set_sleep(bool sleep) { m_sleepOnShow = sleep; }

protected:
    static const int DONE = -1;
    static const uint64_t BUSY_TIMEOUT_US = 30000000;

    virtual int runStep(int step, uint64_t now_us) = 0;
    virtual int onTimeout(int step, uint64_t now_us);
    virtual void requestStatus() {}

    void waitDelay(uint64_t now_us, uint64_t delay_us);
    void waitBusy(bool level, uint64_t now_us, uint64_t timeout_us = BUSY_TIMEOUT_US);

    static void setReset(bool level);
    static void sendCommand(uint8_t command, std::initializer_list<uint8_t> data = {});
    static void sendData(const uint8_t *data, size_t size);

    const uint8_t *m_image = nullptr;
    size_t m_imageSize = 0;
    uint64_t m_startUs = 0;
    uint64_t m_busyPollUs = 0;

private:
    enum Wait
    {
        WAIT_NONE,
        WAIT_DELAY,
        WAIT_BUSY_HIGH,
        WAIT_BUSY_LOW,
    };

    bool waitOver(uint64_t now_us);
    void logBusyWaitOver(const char *how, bool high, uint64_t waited_us);
    void checkPanelResponded();
    static void onBusyIrq();

    int m_step = DONE;
    Wait m_wait = WAIT_NONE;
    uint64_t m_waitStartUs = 0;
    uint64_t m_waitUs = 0;
    uint64_t m_lastPollUs = 0;
    uint32_t m_lastBusyHighCount = 0;
    uint32_t m_lastBusyLowCount = 0;
    uint32_t m_startBusyHighCount = 0;
    uint32_t m_startBusyLowCount = 0;
    bool m_timedOut = false;
    bool m_failed = false;
    bool m_sleepOnShow = false;

    static ScreenStateMachine *m_instance;

    static volatile uint32_t m_busyHighCount;
    static volatile uint32_t m_busyLowCount;
};
