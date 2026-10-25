/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#ifndef AI_LOG_LINES
#define AI_LOG_LINES 24
#endif

#ifndef AI_LOG_LINE_BYTES
#define AI_LOG_LINE_BYTES 112
#endif

class AiLog
{
public:
    static AiLog& get();

    void set_now(uint64_t now_us) { m_now_ms = (uint32_t)(now_us / 1000); }

    void add(const char *format, ...) __attribute__((format(printf, 2, 3)));

    uint32_t last() const { return m_next - 1; }

    uint32_t after(uint32_t seq, char *out, uint32_t size) const;

private:
    AiLog() = default;

    char m_lines[AI_LOG_LINES][AI_LOG_LINE_BYTES] = {};
    uint32_t m_ms[AI_LOG_LINES] = {};
    uint32_t m_next = 1;
    uint32_t m_now_ms = 0;
};
