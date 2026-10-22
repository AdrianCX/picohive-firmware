/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "os_fs.h"
#include "pico_logger.h"
#include "wipefilesystem.h"

bool WipeFilesystem::on_header(CommandContext& ctx)
{
    trace("wipefilesystem: formatting");
    os_fs_format();

    return ctx.reply("filesystem wiped, reboot to come up clean");
}

WS_COMMAND(WipeFilesystem);
