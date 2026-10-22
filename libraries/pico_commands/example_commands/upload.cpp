/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>

#include "pico_logger.h"
#include "upload.h"

Upload::~Upload()
{
    if (m_open)
    {
        os_fs_close(&m_file);
    }
}

bool Upload::on_header(CommandContext& ctx)
{
    if (ctx.args.empty() || (ctx.args.size() >= MAX_FILENAME))
    {
        return ctx.reply_fmt(48, "upload/<filename>, at most %u characters", MAX_FILENAME - 1);
    }

    snprintf(m_name, sizeof(m_name), "%.*s", (int)ctx.args.size(), ctx.args.data());

    if (os_fs_open(&m_file, m_name, OS_FS_WRITE | OS_FS_CREATE | OS_FS_TRUNCATE) != 0)
    {
        return ctx.reply_fmt(80, "cannot open %s", m_name);
    }

    m_open = true;
    return true;
}

bool Upload::on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk)
{
    if (!m_open)
    {
        return true;
    }

    if (os_fs_write(&m_file, data, len) < (int32_t)len)
    {
        on_abort(ctx, "the write failed");
        return ctx.reply_fmt(112, "write failed after %u bytes, %s removed", m_written, m_name);
    }

    m_written += len;

    if (!last_chunk)
    {
        return true;
    }

    os_fs_close(&m_file);
    m_open = false;

    return ctx.reply_fmt(96, "stored %u bytes in %s", m_written, m_name);
}

void Upload::on_abort(CommandContext&, const char *reason)
{
    if (!m_open)
    {
        return;
    }

    os_fs_close(&m_file);
    os_fs_remove(m_name);
    m_open = false;

    trace("upload: %s cut off after %u bytes, removed", m_name, m_written);
}

MQTT_COMMAND(Upload);
WS_COMMAND(Upload);
