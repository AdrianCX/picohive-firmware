/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"
#include "reboot.h"
#include "common_functions.h"

bool Reboot::on_header(CommandContext& ctx)
{
    request_restart(RESTART_REBOOT_COMMAND);

    return ctx.reply(restart_allowed() ? "restarting" : "restarting once the app is idle");
}

MQTT_COMMAND(Reboot);
WS_COMMAND(Reboot);
