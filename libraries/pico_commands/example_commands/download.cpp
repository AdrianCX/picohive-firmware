/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>

#include "download.h"
#include "pico_logger.h"

bool Download::on_header(CommandContext& ctx)
{
    if (ctx.args.empty() || (ctx.args.size() >= MAX_FILENAME))
    {
        return ctx.reply_fmt(52, "download/<filename>, at most %u characters", MAX_FILENAME - 1);
    }

    snprintf(m_name, sizeof(m_name), "%.*s", (int)ctx.args.size(), ctx.args.data());

    if (!m_stream.open(m_name))
    {
        return ctx.reply_fmt(80, "cannot open %s", m_name);
    }

    snprintf(m_topic, sizeof(m_topic), "%.*s/%s", (int)name.size(), name.data(), m_name);

    if (!ctx.begin_stream(m_topic, m_stream.remaining()))
    {
        m_stream.close();
        return false;
    }

    return m_stream.pump(ctx);
}

bool Download::on_send_space(CommandContext& ctx)
{
    return m_stream.pump(ctx);
}

void Download::on_abort(CommandContext&, const char *reason)
{
    if (m_stream.is_open())
    {
        trace("download: %s stopped with %u bytes left", m_name, m_stream.remaining());
        m_stream.close();
    }
}

MQTT_COMMAND(Download);
WS_COMMAND(Download);
