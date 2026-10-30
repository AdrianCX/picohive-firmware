/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"

#define AI_SETTLE_US 250000ULL

class AiTools
{
public:
    static constexpr const char *REFUSED = "no such tool, or its args are too long";
    static constexpr const char *NO_ANSWER = "done, no answer";

    typedef bool (*Ready)();

    typedef bool (*Busy)();

    static uint32_t tool_name(const Command& command, char *out, uint32_t size);

    static const Command *find_tool(const char *name);

    static bool make_topic(const Command& command, const char *args, char *out, uint32_t size);

    static void set_instructions(const char *instructions) { s_instructions = instructions; }
    static const char *instructions() { return s_instructions; }

    static void set_ready(Ready ready) { s_ready = ready; }
    static bool has_ready() { return s_ready != NULL; }

    static void set_busy(Busy busy) { s_busy = busy; }

    static bool done_after(uint64_t since_us, uint64_t timeout_us);

    static bool finished_after(uint64_t since_us, uint64_t timeout_us, uint64_t busy_timeout_us);

private:
    static const char *s_instructions;
    static Ready s_ready;
    static Busy s_busy;
};
