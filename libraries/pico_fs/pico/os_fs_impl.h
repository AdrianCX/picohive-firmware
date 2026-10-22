/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// os_fs on the board: a file is a littlefs file.
#pragma once

#include <stdbool.h>

#include "lfs.h"

typedef struct os_file {
    lfs_file_t file;
    bool open;
} os_file_t;
