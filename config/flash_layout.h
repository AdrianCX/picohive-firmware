/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

// Flash layout:
//   [ firmware slot ][ staging slot ][ ... ][ filesystem ]

#include "hardware/flash.h"

#if PICO_RP2040
// 2MB of flash: 400K of filesystem at the end, the rest split in two slots.
#ifndef PICO_OS_FS_SIZE
#define PICO_OS_FS_SIZE         (100 * 4096)
#endif
#ifndef PICO_OS_FS_OFFSET
#define PICO_OS_FS_OFFSET       (PICO_FLASH_SIZE_BYTES - PICO_OS_FS_SIZE)
#endif
#ifndef PICO_OS_SLOT_SIZE
#define PICO_OS_SLOT_SIZE       (206 * 4096)
#endif
#else
// 4MB of flash: two 1MB slots, then 2MB of filesystem.
#ifndef PICO_OS_FS_SIZE
#define PICO_OS_FS_SIZE         (2 * 1024 * 1024)
#endif
#ifndef PICO_OS_FS_OFFSET
#define PICO_OS_FS_OFFSET       (0x200000)
#endif
#ifndef PICO_OS_SLOT_SIZE
#define PICO_OS_SLOT_SIZE       (0x100000)
#endif
#endif

#define PICO_OS_FIRMWARE_OFFSET (0)
#define PICO_OS_STAGING_OFFSET  (PICO_OS_FIRMWARE_OFFSET + PICO_OS_SLOT_SIZE)

#ifdef __cplusplus
#define PICO_OS_STATIC_ASSERT static_assert
#else
#define PICO_OS_STATIC_ASSERT _Static_assert
#endif

PICO_OS_STATIC_ASSERT((PICO_OS_SLOT_SIZE % FLASH_SECTOR_SIZE) == 0, "flash slots must be whole sectors");
PICO_OS_STATIC_ASSERT((PICO_OS_FS_OFFSET % FLASH_SECTOR_SIZE) == 0, "the filesystem must start on a sector");
PICO_OS_STATIC_ASSERT((PICO_OS_FS_SIZE % FLASH_SECTOR_SIZE) == 0, "the filesystem must be whole sectors");
PICO_OS_STATIC_ASSERT(PICO_OS_STAGING_OFFSET + PICO_OS_SLOT_SIZE <= PICO_OS_FS_OFFSET, "the staging slot runs into the filesystem");
PICO_OS_STATIC_ASSERT(PICO_OS_FS_OFFSET + PICO_OS_FS_SIZE <= PICO_FLASH_SIZE_BYTES, "the filesystem runs past the end of flash");
