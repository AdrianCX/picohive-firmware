/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct CalendarStatus
{
    static constexpr std::string_view name = "calendar";
    static constexpr std::string_view help = "calendar - the board time, the last fetch and the next 5 commands";

    bool on_header(CommandContext& ctx);
};
