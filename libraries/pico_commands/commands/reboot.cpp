/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"
#include "reboot.h"

bool Reboot::on_header(CommandContext& ctx)
{
    trace("reboot: restarting");
    force_restart(RESTART_REBOOT_COMMAND);
    return true;
}

MQTT_COMMAND(Reboot);
WS_COMMAND(Reboot);
