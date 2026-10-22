/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OS_UNIQUE_ID_SIZE 8

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "pico/unique_id.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "lwip/arch.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/opt.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void os_unique_id(uint8_t id[OS_UNIQUE_ID_SIZE])
{
    pico_unique_board_id_t board;
    pico_get_unique_board_id(&board);
    memcpy(id, board.id, OS_UNIQUE_ID_SIZE);
}

#ifdef __cplusplus
}
#endif
