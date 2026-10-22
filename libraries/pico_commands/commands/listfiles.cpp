/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>

#include "os_fs.h"
#include "pico_logger.h"
#include "listfiles.h"


bool ListFiles::on_header(CommandContext& ctx)
{
    ReplyBuffer text(ctx, 1024);
    if (!text)
    {
        return ctx.reply("no memory for the reply");
    }

    // Stops once the listing is full.
    int err = os_fs_list("", [&](const char *file, uint32_t size, bool is_dir) 
    {
        return is_dir || text.append_snprintf("%u %s\n", (unsigned int)size, file);
    });

    if (err < 0)
    {
        trace("ListFiles: cannot open root directory: %d", err);
        return ctx.reply_fmt(40, "cannot open root directory: %d", err);
    }

    trace("ListFiles: %s", text.c_str());

    return ctx.reply(text);
}

MQTT_COMMAND(ListFiles);
