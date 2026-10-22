/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "file_stream.h"

struct Download
{
    static constexpr std::string_view name = "download";
    static constexpr std::string_view help = "download/<file> - stream a file out of littlefs";

    static constexpr bool streams_output = true;

    bool on_header(CommandContext& ctx);
    bool on_send_space(CommandContext& ctx);
    void on_abort(CommandContext& ctx, const char *reason);

private:
    static const uint32_t MAX_FILENAME = 64;

    char m_name[MAX_FILENAME] = {};
    char m_topic[sizeof("download/") + MAX_FILENAME] = {};
    FileStream m_stream;
};
