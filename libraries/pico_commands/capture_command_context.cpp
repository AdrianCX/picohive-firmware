/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "capture_command_context.h"

bool CaptureCommandContext::run(std::span<const Command> table, std::string_view topic, uint32_t limit,
                                std::string_view body)
{
    left = limit;
    written = 0;

    return CommandDispatcher::run_once(table, *this, topic, body);
}

void CaptureCommandContext::run_each(std::span<const Command> table, uint32_t limit)
{
    bool first = true;

    for (const Command& command : table)
    {
        if (!first)
        {
            take("\n\n", 2);
        }

        take(command.name.data(), command.name.size());
        take(":\n", 2);
        run(table, command.name, limit);
        first = false;
    }
}

bool CaptureCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    if ((written > 0) && (left > 0))
    {
        take("\n", 1);
        --left;
    }

    return write_chunk(data, len);
}

bool CaptureCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    return true;
}

bool CaptureCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    uint32_t taken = (len < left) ? len : left;

    take((const char *)data, taken);
    left -= taken;
    written += taken;
    return true;
}
