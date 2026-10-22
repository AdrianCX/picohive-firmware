/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#pragma once

#include "command_registration.h"

struct Start
{
    static constexpr std::string_view name = "start";
    static constexpr std::string_view help = "start - show the buttons";

    bool on_header(CommandContext& ctx);
};
