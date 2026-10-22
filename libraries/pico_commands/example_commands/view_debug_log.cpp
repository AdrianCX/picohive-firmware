/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "view_debug_log.h"

bool ViewDebugLog::on_header(CommandContext& ctx)
{
    if (!m_log.open("debug.txt"))
    {
        return ctx.reply("no offline log - see set-debug-log");
    }

    if (!ctx.begin_stream("viewofflinelog", m_log.remaining()))
    {
        m_log.close();
        return false;
    }

    return m_log.pump(ctx);
}

bool ViewDebugLog::on_send_space(CommandContext& ctx)
{
    return m_log.pump(ctx);
}

void ViewDebugLog::on_abort(CommandContext&, const char *reason)
{
    m_log.close();
}

MQTT_COMMAND(ViewDebugLog);
