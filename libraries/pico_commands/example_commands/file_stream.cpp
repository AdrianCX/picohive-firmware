/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "file_stream.h"
#include "pico_logger.h"

bool FileStream::open(const char *path)
{
    close();

    if (os_fs_open(&m_file, path, OS_FS_READ) != 0)
    {
        return false;
    }

    int32_t size = os_fs_size(&m_file);

    if (size < 0)
    {
        os_fs_close(&m_file);
        return false;
    }

    m_open = true;
    m_remaining = size;
    return true;
}

bool FileStream::pump(CommandContext& ctx)
{
    while (m_remaining > 0)
    {
        uint32_t space = ctx.send_space();

        if (space == 0)
        {
            ctx.send_more_later();
            return true;
        }

        uint32_t wanted = (space < CHUNK) ? space : CHUNK;
        if (wanted > m_remaining)
        {
            wanted = m_remaining;
        }

        int32_t bytes = os_fs_read(&m_file, m_buffer, wanted);

        if ((bytes <= 0) || !ctx.send_chunk(m_buffer, bytes))
        {
            trace("file stream: failed with %u bytes left", m_remaining);
            close();
            return false;
        }

        m_remaining -= bytes;
    }

    close();
    ctx.sending_done();
    return true;
}

void FileStream::close()
{
    if (m_open)
    {
        os_fs_close(&m_file);
        m_open = false;
    }

    m_remaining = 0;
}
