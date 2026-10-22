/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "help.h"

bool Help::on_header(CommandContext& ctx)
{
    std::span<const Command> commands = (ctx.prefix == ws_prefix_of<Help>()) ? WsCommandSet::all() : MqttCommandSet::all();

    for (const Command& c : commands)
    {
        if (c.payload == Payload::None)
        {
            ctx.reply_fmt(512, "%.*s%.*s: %.*s", (int)c.prefix.size(), c.prefix.data(), (int)c.name.size(), c.name.data(), (int)c.help.size(), c.help.data());
        }
        else
        {
            ctx.reply_fmt(512, "%.*s%.*s [%s, max %u bytes]: %.*s", (int)c.prefix.size(), c.prefix.data(), (int)c.name.size(), c.name.data(), (c.payload == Payload::Buffered) ? "buffered" : "streamed", c.max_bytes, (int)c.help.size(), c.help.data());
        }
    }

    return true;
}

MQTT_COMMAND(Help);
WS_COMMAND(Help);
