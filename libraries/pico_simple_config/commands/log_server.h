/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct LogServer
{
    static constexpr std::string_view name = "log/server";
    static constexpr std::string_view help = "Log server overrides (body is the key): log/server/<host>[/<port>], log/server/default; read settings: log/server";
    static constexpr std::string_view ai_help = "log/server/<host>[/<port>] or /default, body: log key";

    static constexpr Payload payload = Payload::Buffered;
    static constexpr uint32_t max_bytes = 128;

    bool on_payload(CommandContext& ctx, std::string_view body);
};
