/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct SetDebugLog
{
    static constexpr std::string_view name = "set-debug-log";
    static constexpr std::string_view help = "set-debug-log/<bytes> - keep that much trace in littlefs, 0 for none";

    bool on_header(CommandContext& ctx);
};
