/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "os_fs.h"
#include "os_platform.h"
#include "os_linux.h"
#include "pico_logger.h"

#define NAME_MAX_LEN 255

static char root[4096];

static int error(void)
{
    return (errno != 0) ? -errno : -EIO;
}

// The full path of name under the root; ".." anywhere is refused.
static int full_path(const char *name, char *out, size_t len)
{
    if ((name == NULL) || (root[0] == 0))
    {
        return -EINVAL;
    }

    while (*name == '/')
    {
        name++;
    }

    for (const char *at = name; *at != 0; )
    {
        const char *end = strchr(at, '/');
        size_t part = (end != NULL) ? (size_t)(end - at) : strlen(at);

        if ((part == 2) && (at[0] == '.') && (at[1] == '.'))
        {
            return -EINVAL;
        }
        if (part > NAME_MAX_LEN)
        {
            return -ENAMETOOLONG;
        }
        at += part;
        while (*at == '/')
        {
            at++;
        }
    }

    int n = snprintf(out, len, "%s/%s", root, name);
    return ((n < 0) || ((size_t)n >= len)) ? -ENAMETOOLONG : 0;
}

void os_fs_init(void)
{
    snprintf(root, sizeof(root), "%s/fs", os_data_dir());

    if ((mkdir(root, 0700) != 0) && (errno != EEXIST))
    {
        fail("os_fs_init: cannot create %s: %s", root, strerror(errno));
    }
}

const char *os_fs_root(void)
{
    return root;
}

int os_fs_open(os_file_t *file, const char *name, int flags)
{
    char path[4352];
    file->fd_plus_one = 0;

    int err = full_path(name, path, sizeof(path));
    if (err != 0)
    {
        return err;
    }

    int mode = ((flags & OS_FS_READ) && (flags & OS_FS_WRITE)) ? O_RDWR : (flags & OS_FS_WRITE) ? O_WRONLY : O_RDONLY;
    mode |= O_CLOEXEC;
    mode |= (flags & OS_FS_CREATE) ? O_CREAT : 0;
    mode |= (flags & OS_FS_TRUNCATE) ? O_TRUNC : 0;
    mode |= (flags & OS_FS_APPEND) ? O_APPEND : 0;

    int fd = open(path, mode, 0600);
    if (fd < 0)
    {
        return error();
    }

    file->fd_plus_one = fd + 1;
    return 0;
}

int32_t os_fs_read(os_file_t *file, void *data, uint32_t len)
{
    if (file->fd_plus_one <= 0)
    {
        return -EBADF;
    }

    uint32_t done = 0;
    while (done < len)
    {
        ssize_t n = read(file->fd_plus_one - 1, (char *)data + done, len - done);
        if (n == 0)
        {
            break;
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return error();
        }
        done += n;
    }
    return (int32_t)done;
}

int32_t os_fs_write(os_file_t *file, const void *data, uint32_t len)
{
    if (file->fd_plus_one <= 0)
    {
        return -EBADF;
    }

    uint32_t done = 0;
    while (done < len)
    {
        ssize_t n = write(file->fd_plus_one - 1, (const char *)data + done, len - done);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return error();
        }
        done += n;
    }
    return (int32_t)done;
}

int32_t os_fs_size(os_file_t *file)
{
    if (file->fd_plus_one <= 0)
    {
        return -EBADF;
    }

    struct stat st;
    if (fstat(file->fd_plus_one - 1, &st) != 0)
    {
        return error();
    }
    return (int32_t)st.st_size;
}

int os_fs_sync(os_file_t *file)
{
    if (file->fd_plus_one <= 0)
    {
        return -EBADF;
    }
    return (fsync(file->fd_plus_one - 1) == 0) ? 0 : error();
}

int os_fs_close(os_file_t *file)
{
    if (file->fd_plus_one <= 0)
    {
        return -EBADF;
    }

    int fd = file->fd_plus_one - 1;
    file->fd_plus_one = 0;
    return (close(fd) == 0) ? 0 : error();
}

int os_fs_remove(const char *name)
{
    char path[4352];
    int err = full_path(name, path, sizeof(path));
    if (err != 0)
    {
        return err;
    }

    struct stat st;
    if (stat(path, &st) != 0)
    {
        return error();
    }

    // A file, or an empty directory, as littlefs.
    return ((S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path)) == 0) ? 0 : error();
}

int os_fs_list(const char *dir, os_fs_entry_fn entry, void *context)
{
    char path[4352];
    int err = full_path((dir != NULL) ? dir : "", path, sizeof(path));
    if (err != 0)
    {
        return err;
    }

    DIR *handle = opendir(path);
    if (handle == NULL)
    {
        return error();
    }

    struct dirent *item;
    while ((item = readdir(handle)) != NULL)
    {
        if ((strcmp(item->d_name, ".") == 0) || (strcmp(item->d_name, "..") == 0))
        {
            continue;
        }

        struct stat st;
        if (fstatat(dirfd(handle), item->d_name, &st, 0) != 0)
        {
            continue;
        }

        if (!entry(context, item->d_name, S_ISDIR(st.st_mode) ? 0 : (uint32_t)st.st_size, S_ISDIR(st.st_mode)))
        {
            break;
        }
    }

    closedir(handle);
    return 0;
}

int os_fs_format(void)
{
    char aside[4096 + 64];
    snprintf(aside, sizeof(aside), "%s.wiped-%ld", root, (long)time(NULL));

    if ((rename(root, aside) != 0) && (errno != ENOENT))
    {
        return error();
    }

    trace("os_fs_format: the old files are in %s", aside);

    if (mkdir(root, 0700) != 0)
    {
        return error();
    }
    return 0;
}
