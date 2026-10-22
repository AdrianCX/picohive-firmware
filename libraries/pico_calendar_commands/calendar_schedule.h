/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include <string_view>

#ifndef CALENDAR_MAX_PENDING
#define CALENDAR_MAX_PENDING 16
#endif

#ifndef CALENDAR_MAX_FIRED
#define CALENDAR_MAX_FIRED 32
#endif

#ifndef CALENDAR_LATE_S
#define CALENDAR_LATE_S 900
#endif

// Longest command an event title can carry, after the id; a command's args
// stop at 64 anyway (CommandSlot).
#ifndef CALENDAR_COMMAND_CAP
#define CALENDAR_COMMAND_CAP 64
#endif

// "<id>/<command>" -> "<command>", if the title is for this id.
bool calendar_route(std::string_view title, std::string_view id, std::string_view& command);
bool calendar_allowed(std::string_view allowed, std::string_view command);

class CalendarSchedule
{
public:
    void clock_known(int64_t now_utc);
    void clear() { m_count = 0; }

    // False if the list is full; add earliest first.
    bool add(int64_t start_utc, std::string_view command);

    uint32_t count() const { return m_count; }
    int64_t start_at(uint32_t i) const { return m_entries[i].start_utc; }
    std::string_view command_at(uint32_t i) const { return std::string_view(m_entries[i].command, m_entries[i].len); }

    // Calls run(command, start_utc) once for every event due by now_utc.
    template <typename Run>
    uint32_t fire_due(int64_t now_utc, Run run)
    {
        uint32_t ran = 0;

        for (uint32_t i = 0; i < m_count; ++i)
        {
            const Entry& entry = m_entries[i];
            uint32_t hash = hash_of(entry);

            if (!is_due(entry, now_utc) || has_fired(entry.start_utc, hash))
            {
                continue;
            }

            remember(entry.start_utc, hash);
            run(std::string_view(entry.command, entry.len), entry.start_utc);
            ++ran;
        }

        return ran;
    }

    void reset();

private:
    struct Entry
    {
        int64_t start_utc = 0;
        uint16_t len = 0;
        char command[CALENDAR_COMMAND_CAP] = {};
    };

    struct Fired
    {
        int64_t start_utc = 0;
        uint32_t hash = 0;
    };

    bool is_due(const Entry& entry, int64_t now_utc) const;
    bool has_fired(int64_t start_utc, uint32_t hash) const;
    void remember(int64_t start_utc, uint32_t hash);
    static uint32_t hash_of(const Entry& entry);

    bool m_floor_set = false;
    int64_t m_floor_utc = 0;

    Entry m_entries[CALENDAR_MAX_PENDING];
    uint32_t m_count = 0;

    Fired m_fired[CALENDAR_MAX_FIRED];
    uint32_t m_fired_count = 0;
    uint32_t m_fired_next = 0;
};
