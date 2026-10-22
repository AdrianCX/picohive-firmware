/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include "app_config.h"
#include "command_registration.h"

struct ConfigRead
{
    static constexpr std::string_view name = "config/read";
    static constexpr std::string_view help = "config/read - every setting as JSON, secrets only as set or not";

    bool on_header(CommandContext& ctx);
};
