/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct DeleteFile
{
    static constexpr std::string_view name = "rm";
    static constexpr std::string_view help = "rm - remove file in littlefs";
    static constexpr std::string_view ws_prefix = "/dbg/";

    static constexpr Payload payload = Payload::Buffered;
    static constexpr uint32_t max_bytes = 128;

    bool on_payload(CommandContext& ctx, std::string_view body);
};
