/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "config_read.h"
#include "config_store.h"

bool ConfigRead::on_header(CommandContext& ctx)
{
    ReplyBuffer settings(ctx, CONFIG_MAX_JSON);
    if (!settings)
    {
        return ctx.reply("no memory for the reply");
    }

    uint32_t len = ConfigStore::masked(settings.data(), settings.capacity() + 1);
    if (len == 0)
    {
        return ctx.reply("settings do not fit the reply");
    }

    settings.set_size(len);
    return ctx.reply(settings);
}

WS_COMMAND(ConfigRead);
