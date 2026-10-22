/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"

#include "command_table.h"

bool CommandContext::send_reply(const ReplyBuffer& text)
{
    std::string_view bound = prefix;
    prefix = text.prefix();

    bool sent = send_data(text.name(), (const uint8_t *)text.c_str(), text.size());
    prefix = bound;

    return sent;
}

bool CommandContext::queue(const ReplyBuffer& text)
{
    if (m_waiting.empty() || m_waiting[m_queueTail])
    {
        trace("commands: no room to queue %.*s, %u answers waiting", (int)text.name().size(), text.name().data(), (unsigned int)m_waiting.size());
        return false;
    }

    m_waiting[m_queueTail] = text;
    m_queueTail = (m_queueTail + 1) % m_waiting.size();
    return true;
}

bool CommandContext::flush_queued()
{
    while (has_queued())
    {
        ReplyBuffer& oldest = m_waiting[m_queueHead];

        if (send_space() < message_cost(oldest.name(), oldest.size()))
        {
            return true;
        }

        if (!send_reply(oldest))
        {
            trace("commands: the link refused a queued answer for %.*s, dropping what was waiting",
                  (int)oldest.name().size(), oldest.name().data());
            drop_queued();
            return false;
        }

        oldest = ReplyBuffer();
        m_queueHead = (m_queueHead + 1) % m_waiting.size();
    }

    return true;
}

void CommandContext::drop_queued()
{
    for (ReplyBuffer& waiting : m_waiting)
    {
        waiting = ReplyBuffer();
    }

    m_queueHead = 0;
    m_queueTail = 0;
}
