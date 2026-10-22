/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct WipeFilesystem
{
    static constexpr std::string_view name = "wipefilesystem";
    static constexpr std::string_view help = "wipefilesystem - format littlefs, wiping wifi and mqtt config";
    static constexpr std::string_view ws_prefix = "/dbg/";

    bool on_header(CommandContext& ctx);
};
