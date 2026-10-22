/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "os_fs_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum os_fs_flags {
    OS_FS_READ     = 1,
    OS_FS_WRITE    = 2,
    OS_FS_CREATE   = 4,
    OS_FS_TRUNCATE = 8,
    OS_FS_APPEND   = 16,
};

void os_fs_init(void);
const char *os_fs_root(void);

int os_fs_open(os_file_t *file, const char *name, int flags);
int32_t os_fs_read(os_file_t *file, void *data, uint32_t len);
int32_t os_fs_write(os_file_t *file, const void *data, uint32_t len);
int32_t os_fs_size(os_file_t *file);
int os_fs_sync(os_file_t *file);
int os_fs_close(os_file_t *file);

int os_fs_remove(const char *name);

typedef bool (*os_fs_entry_fn)(void *context, const char *name, uint32_t size, bool is_dir);
int os_fs_list(const char *dir, os_fs_entry_fn entry, void *context);

int os_fs_format(void);

#ifdef __cplusplus
}

#include <type_traits>

template <typename Entry>
int os_fs_list(const char *dir, Entry &&entry)
{
    using E = std::remove_reference_t<Entry>;

    return os_fs_list(dir, [](void *context, const char *name, uint32_t size, bool is_dir) -> bool {
        return (*static_cast<E *>(context))(name, size, is_dir);
    }, (void *)&entry);
}
#endif
