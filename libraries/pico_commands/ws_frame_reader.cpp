/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "ws_frame_reader.h"

bool WsFrameReader::feed(CommandDispatcher& dispatcher, uint8_t *data, uint32_t len, uint64_t offset, uint64_t length)
{
    bool last_chunk = ((offset + len) >= length);

    if (offset == 0)
    {
        m_path_len = 0;
        m_in_path = true;
        m_too_long = false;
        m_dispatched = false;
    }

    if (!m_in_path)
    {
        return (m_dispatched && dispatcher.is_streaming()) ? dispatcher.dispatch_data(data, len, last_chunk) : true;
    }

    uint32_t i = 0;
    for (; (i < len) && (data[i] != 0); ++i)
    {
        if (m_path_len < WS_PATH_LEN)
        {
            m_path[m_path_len++] = (char)data[i];
        }
        else
        {
            m_too_long = true;
        }
    }

    bool found_nul = (i < len);

    if (!found_nul && !last_chunk)
    {
        return true;
    }

    m_in_path = false;

    uint32_t used = found_nul ? (i + 1) : i;
    uint64_t body_start = offset + used;
    uint32_t body_len = (length > body_start) ? (uint32_t)(length - body_start) : 0;

    if (m_too_long)
    {
        return true;
    }

    if (!dispatcher.dispatch_header(std::string_view(m_path, m_path_len), body_len))
    {
        return false;
    }

    m_dispatched = true;

    if (dispatcher.is_streaming() && (used < len))
    {
        return dispatcher.dispatch_data(data + used, len - used, last_chunk);
    }

    return true;
}
