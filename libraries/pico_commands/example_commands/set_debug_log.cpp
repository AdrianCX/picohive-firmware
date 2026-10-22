/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdint.h>

#include "app_config.h"
#include "config_store.h"
#include "pico_logger.h"
#include "set_debug_log.h"

bool SetDebugLog::on_header(CommandContext& ctx)
{
    uint32_t size = 0;
    bool valid = !ctx.args.empty();

    for (char c : ctx.args)
    {
        if ((c < '0') || (c > '9') || (size > (INT32_MAX - (uint32_t)(c - '0')) / 10))
        {
            valid = false;
            break;
        }

        size = size * 10 + (c - '0');
    }

    if (!valid)
    {
        return ctx.reply_fmt(104, "expected set-debug-log/<bytes>, got [%.*s]", (int)ctx.args.size(), ctx.args.data());
    }

    StoredConfig stored;

    if (!stored.set_field("debug", "log_size", size) || !stored.save())
    {
        return ctx.reply("saving failed");
    }

    get_debug_config().offline_log_size = size;

    int status = setup_debug_file((int32_t)size);

    return ctx.reply_fmt(48, "offline log %u bytes (status %d)", size, status);
}

MQTT_COMMAND(SetDebugLog);
