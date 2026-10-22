/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "os_fs.h"

struct Upload
{
    static constexpr std::string_view name = "upload";
    static constexpr std::string_view help = "upload/<file> - stream a body into littlefs";

    static constexpr Payload payload = Payload::Streamed;
    static constexpr uint32_t max_bytes = 32 * 1024;
    static constexpr bool clears_retained = true;   // published with -r it would be written on every connect

    ~Upload();

    bool on_header(CommandContext& ctx);
    bool on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk);
    void on_abort(CommandContext& ctx, const char *reason);

private:
    static const uint32_t MAX_FILENAME = 64;

    os_file_t m_file = {};
    bool m_open = false;
    char m_name[MAX_FILENAME] = {};
    uint32_t m_written = 0;
};
