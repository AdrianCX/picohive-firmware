/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "calendar_commands.h"
#include "calendar_refresh.h"

bool CalendarRefresh::on_header(CommandContext& ctx)
{
    if (!CalendarCommands::get().refresh())
    {
        return ctx.reply("no calendar address or device id set");
    }

    return ctx.reply("fetching");
}

MQTT_COMMAND(CalendarRefresh);
WS_COMMAND(CalendarRefresh);
TELEGRAM_COMMAND(CalendarRefresh);
