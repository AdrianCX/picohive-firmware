/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "os_fs.h"

class FileStream
{
public:
    ~FileStream() { close(); }

    bool open(const char *path);
    bool pump(CommandContext& ctx);
    void close();

    bool is_open() const { return m_open; }
    uint32_t remaining() const { return m_remaining; }

private:
    static const uint32_t CHUNK = 512;

    os_file_t m_file = {};
    bool m_open = false;
    uint32_t m_remaining = 0;
    uint8_t m_buffer[CHUNK];
};
