/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdarg.h>
#include <stdio.h>

#include "ai_log.h"

AiLog& AiLog::get()
{
    static AiLog instance;
    return instance;
}

void AiLog::add(const char *format, ...)
{
    char *line = m_lines[m_next % AI_LOG_LINES];
    m_ms[m_next % AI_LOG_LINES] = m_now_ms;

    va_list ap;
    va_start(ap, format);
    vsnprintf(line, AI_LOG_LINE_BYTES, format, ap);
    va_end(ap);

    for (char *c = line; *c != 0; ++c)
    {
        if ((*c == '\n') || (*c == '\r'))
        {
            *c = ' ';
        }
    }

    ++m_next;
}

uint32_t AiLog::after(uint32_t seq, char *out, uint32_t size) const
{
    uint32_t first = seq + 1;

    if ((m_next > AI_LOG_LINES) && (first < (m_next - AI_LOG_LINES)))
    {
        first = m_next - AI_LOG_LINES;
    }

    uint32_t len = 0;

    for (uint32_t i = first; i < m_next; ++i)
    {
        int n = snprintf(&out[len], size - len, "%u %u %s\n", (unsigned int)i, (unsigned int)m_ms[i % AI_LOG_LINES],
                         m_lines[i % AI_LOG_LINES]);

        if ((n < 0) || ((uint32_t)n >= (size - len)))
        {
            break;
        }

        len += n;
    }

    if (len < size)
    {
        out[len] = 0;
    }

    return len;
}
