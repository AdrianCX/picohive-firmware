/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// pico_sleep on linux: no deep sleep. Every call fails, so the app keeps
// running its loop; there is never a wake reason.

#include <stdbool.h>
#include <stdint.h>

#include "pico_sleep.h"
#include "pico_logger.h"

bool pico_deepsleep_init(uint64_t sleep_time_ms)
{
    trace("pico_deepsleep_init: mock, no deep sleep on linux, sleep_time_ms[%llu]", (unsigned long long)sleep_time_ms);
    return false;
}

void pico_deepsleep_set_gpio(int hw_instance, uint8_t gpio, bool edge, bool high)
{
}

bool pico_deepsleep_execute()
{
    return false;
}

bool pico_deepsleep_wake_reason_gpio(uint8_t hw_instance)
{
    return false;
}

bool pico_deepsleep(uint64_t sleep_time_ms)
{
    return pico_deepsleep_init(sleep_time_ms);
}

uint8_t pico_deepsleep_wake_reason()
{
    return 0;
}

void pico_deepsleep_clear_reason()
{
}
