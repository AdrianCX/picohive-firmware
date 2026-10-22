/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "file_stream.h"

struct ViewDebugLog
{
    static constexpr std::string_view name = "view-debug-log";
    static constexpr std::string_view help = "view-debug-log - stream the offline debug log";

    static constexpr bool streams_output = true;

    bool on_header(CommandContext& ctx);
    bool on_send_space(CommandContext& ctx);
    void on_abort(CommandContext& ctx, const char *reason);

private:
    FileStream m_log;
};
