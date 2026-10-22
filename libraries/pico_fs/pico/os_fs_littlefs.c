/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


// littlefs (littlefs/) stored in region defined by flash_layout
//
// LittleFS is available here:https://github.com/lurk101/pico-littlefs

#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"

#include "flash_layout.h"
#include "lfs.h"
#include "os_fs.h"

typedef struct {
    uint32_t offset;
    const uint8_t *read_base;
} flash_region_t;

static flash_region_t region;
static struct lfs_config config;
static lfs_t lfs;

static int flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    const flash_region_t *r = (const flash_region_t *)c->context;

    memcpy(buffer, r->read_base + (block * c->block_size) + off, size);
    return LFS_ERR_OK;
}

static int flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    const flash_region_t *r = (const flash_region_t *)c->context;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(r->offset + (block * c->block_size) + off, buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    const flash_region_t *r = (const flash_region_t *)c->context;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(r->offset + (block * c->block_size), c->block_size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int flash_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

// Mounts the filesystem, formatting it when there is none (a new device, or
// one wiped).
void os_fs_init(void)
{
    region.offset = PICO_OS_FS_OFFSET;
    region.read_base = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + PICO_OS_FS_OFFSET);

    config.context = &region;
    config.read = flash_read;
    config.prog = flash_prog;
    config.erase = flash_erase;
    config.sync = flash_sync;
    config.read_size = 1;
    config.prog_size = FLASH_PAGE_SIZE;
    config.block_size = FLASH_SECTOR_SIZE;
    config.block_count = PICO_OS_FS_SIZE / FLASH_SECTOR_SIZE;
    config.cache_size = FLASH_SECTOR_SIZE / 4;
    config.lookahead_size = 32;
    config.block_cycles = 500;

    if (lfs_mount(&lfs, &config) != 0)
    {
        lfs_format(&lfs, &config);
        lfs_mount(&lfs, &config);
    }
}

const char *os_fs_root(void)
{
    return "/";
}

int os_fs_open(os_file_t *file, const char *name, int flags)
{
    int lfs_flags = ((flags & OS_FS_READ) && (flags & OS_FS_WRITE)) ? LFS_O_RDWR : (flags & OS_FS_WRITE) ? LFS_O_WRONLY : LFS_O_RDONLY;
    lfs_flags |= (flags & OS_FS_CREATE) ? LFS_O_CREAT : 0;
    lfs_flags |= (flags & OS_FS_TRUNCATE) ? LFS_O_TRUNC : 0;
    lfs_flags |= (flags & OS_FS_APPEND) ? LFS_O_APPEND : 0;

    memset(file, 0, sizeof(*file));

    int err = lfs_file_open(&lfs, &file->file, name, lfs_flags);
    file->open = (err == 0);
    return err;
}

int32_t os_fs_read(os_file_t *file, void *data, uint32_t len)
{
    return file->open ? lfs_file_read(&lfs, &file->file, data, len) : LFS_ERR_BADF;
}

int32_t os_fs_write(os_file_t *file, const void *data, uint32_t len)
{
    return file->open ? lfs_file_write(&lfs, &file->file, data, len) : LFS_ERR_BADF;
}

int32_t os_fs_size(os_file_t *file)
{
    return file->open ? lfs_file_size(&lfs, &file->file) : LFS_ERR_BADF;
}

int os_fs_sync(os_file_t *file)
{
    return file->open ? lfs_file_sync(&lfs, &file->file) : LFS_ERR_BADF;
}

int os_fs_close(os_file_t *file)
{
    if (!file->open)
    {
        return LFS_ERR_BADF;
    }

    file->open = false;
    return lfs_file_close(&lfs, &file->file);
}

int os_fs_remove(const char *name)
{
    return lfs_remove(&lfs, name);
}

int os_fs_list(const char *dir, os_fs_entry_fn entry, void *context)
{
    lfs_dir_t handle;
    struct lfs_info info;

    int err = lfs_dir_open(&lfs, &handle, ((dir == NULL) || (dir[0] == 0)) ? "/" : dir);
    if (err < 0)
    {
        return err;
    }

    while (lfs_dir_read(&lfs, &handle, &info) > 0)
    {
        if ((strcmp(info.name, ".") == 0) || (strcmp(info.name, "..") == 0))
        {
            continue;
        }

        if (!entry(context, info.name, info.size, info.type == LFS_TYPE_DIR))
        {
            break;
        }
    }

    lfs_dir_close(&lfs, &handle);
    return 0;
}

int os_fs_format(void)
{
    lfs_unmount(&lfs);

    int err = lfs_format(&lfs, &config);
    if (err != 0)
    {
        return err;
    }

    return lfs_mount(&lfs, &config);
}
