/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "app_config.h"
#include "config_store.h"
#include "log_server.h"
#include "pico_helper.h"

bool LogServer::on_payload(CommandContext& ctx, std::string_view body)
{
    StoredConfig stored;
    bool changed = false;

    if (!body.empty())
    {
        char key[65] = {};
        uint8_t bytes[32] = {};
        if ((body.size() != 32) && (body.size() != 64))
        {
            return ctx.reply("the key must be 32 or 64 hex characters");
        }
        memcpy(key, body.data(), body.size());
        if (!PicoHelper::parse_hex(key, bytes, body.size() / 2))
        {
            return ctx.reply("the key must be 32 or 64 hex characters");
        }

        if (!stored.set_field("log", "key", key))
        {
            return ctx.reply("saving failed");
        }
        changed = true;
    }

    if (!ctx.args.empty())
    {
        char host[MAX_CONFIG_STR] = {};
        uint32_t port = get_config().logging_port;

        std::string_view args = ctx.args;
        size_t slash = args.find('/');
        std::string_view name = args.substr(0, slash);

        if ((slash != std::string_view::npos) && (!PicoHelper::parse_number(args.substr(slash + 1), port) || (port == 0) || (port > 65534)))
        {
            return ctx.reply("port must be 1-65534");
        }

        if (name.empty() || (name.size() >= sizeof(host)))
        {
            return ctx.reply("log/server/<host>[/<port>] or log/server/default");
        }

        if (name != "default")
        {
            memcpy(host, name.data(), name.size());
        }

        if (!stored.set_field("log", "host", host) || !stored.set_field("log", "port", port))
        {
            return ctx.reply("saving failed");
        }
        changed = true;
    }

    if (changed)
    {
        if (!stored.save())
        {
            return ctx.reply("saving failed");
        }

        apply_config(stored.json());
    }

    return ctx.reply_fmt(96, "%s:%u", get_config().log_server(), (unsigned int)get_config().logging_port);
}

WS_COMMAND(LogServer);
AI_COMMAND(LogServer);
