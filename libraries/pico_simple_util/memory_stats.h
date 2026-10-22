/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void memory_stats_init();

uint32_t getTotalHeap();
uint32_t getUsedHeap();
uint32_t getFreeHeap();
uint32_t getMinFreeHeap();
uint32_t getLargestFreeBlock();
uint32_t getTotalStack();
uint32_t getMaxUsedStack();

#ifdef __cplusplus
}
#endif
