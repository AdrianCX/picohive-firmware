/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

struct Ai
{
    static constexpr std::string_view name = "ai";
    static constexpr std::string_view help = "ai <request> - OpenAI carries it out with the board's commands";

    static constexpr Payload payload = Payload::Buffered;
    static constexpr uint32_t max_bytes = 512;

    bool on_payload(CommandContext& ctx, std::string_view body);
};

struct AiStatus
{
    static constexpr std::string_view name = "ai/status";
    static constexpr std::string_view help = "ai/status - what the ai request is doing, and its last answer";

    bool on_header(CommandContext& ctx);
};

struct AiNew
{
    static constexpr std::string_view name = "ai/new";
    static constexpr std::string_view help = "ai/new - the next ai request starts a new conversation";

    bool on_header(CommandContext& ctx);
};

struct AiEvents
{
    static constexpr std::string_view name = "ai/events";
    static constexpr std::string_view help = "ai/events/<after> - what the ai request did, line by line, after line <after>";

    bool on_header(CommandContext& ctx);
};

struct AiStop
{
    static constexpr std::string_view name = "ai/stop";
    static constexpr std::string_view help = "ai/stop - end the ai request in progress";

    bool on_header(CommandContext& ctx);
};
