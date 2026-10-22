/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "calendar_schedule.h"

static std::string_view trim(std::string_view text)
{
    while (!text.empty() && ((text.front() == ' ') || (text.front() == '\t') || (text.front() == '\r') || (text.front() == '\n')))
    {
        text.remove_prefix(1);
    }

    while (!text.empty() && ((text.back() == ' ') || (text.back() == '\t') || (text.back() == '\r') || (text.back() == '\n')))
    {
        text.remove_suffix(1);
    }

    return text;
}

bool calendar_route(std::string_view title, std::string_view id, std::string_view& command)
{
    title = trim(title);
    id = trim(id);

    if (id.empty() || (title.size() <= id.size()) || !title.starts_with(id) || (title[id.size()] != '/'))
    {
        return false;
    }

    command = trim(title.substr(id.size() + 1));

    while (command.starts_with('/'))
    {
        command.remove_prefix(1);
    }

    return !command.empty();
}

static bool is_separator(char c)
{
    return (c == ' ') || (c == ',') || (c == '\n') || (c == '\r') || (c == '\t');
}

bool calendar_allowed(std::string_view allowed, std::string_view command)
{
    while (!allowed.empty())
    {
        while (!allowed.empty() && is_separator(allowed.front()))
        {
            allowed.remove_prefix(1);
        }

        size_t end = 0;
        while ((end < allowed.size()) && !is_separator(allowed[end]))
        {
            ++end;
        }

        std::string_view entry = allowed.substr(0, end);
        allowed.remove_prefix(end);

        while (entry.starts_with('/'))
        {
            entry.remove_prefix(1);
        }

        while (entry.ends_with('/'))
        {
            entry.remove_suffix(1);
        }

        if (entry.empty() || !command.starts_with(entry))
        {
            continue;
        }

        if ((command.size() == entry.size()) || (command[entry.size()] == '/'))
        {
            return true;
        }
    }

    return false;
}

void CalendarSchedule::clock_known(int64_t now_utc)
{
    if (!m_floor_set)
    {
        m_floor_set = true;
        m_floor_utc = now_utc;
    }
}

bool CalendarSchedule::add(int64_t start_utc, std::string_view command)
{
    if ((m_count >= CALENDAR_MAX_PENDING) || command.empty() || (command.size() >= CALENDAR_COMMAND_CAP))
    {
        return false;
    }

    Entry& entry = m_entries[m_count++];

    entry.start_utc = start_utc;
    entry.len = command.size();
    memcpy(entry.command, command.data(), command.size());
    entry.command[command.size()] = 0;

    return true;
}

void CalendarSchedule::reset()
{
    m_floor_set = false;
    m_floor_utc = 0;
    m_count = 0;
    m_fired_count = 0;
    m_fired_next = 0;
}

bool CalendarSchedule::is_due(const Entry& entry, int64_t now_utc) const
{
    return m_floor_set &&
           (entry.start_utc > m_floor_utc) &&
           (entry.start_utc <= now_utc) &&
           ((now_utc - entry.start_utc) <= CALENDAR_LATE_S);
}

bool CalendarSchedule::has_fired(int64_t start_utc, uint32_t hash) const
{
    for (uint32_t i = 0; i < m_fired_count; ++i)
    {
        if ((m_fired[i].start_utc == start_utc) && (m_fired[i].hash == hash))
        {
            return true;
        }
    }

    return false;
}

void CalendarSchedule::remember(int64_t start_utc, uint32_t hash)
{
    m_fired[m_fired_next] = { start_utc, hash };
    m_fired_next = (m_fired_next + 1) % CALENDAR_MAX_FIRED;

    if (m_fired_count < CALENDAR_MAX_FIRED)
    {
        ++m_fired_count;
    }
}

uint32_t CalendarSchedule::hash_of(const Entry& entry)
{
    uint32_t hash = 2166136261u;

    for (uint16_t i = 0; i < entry.len; ++i)
    {
        hash = (hash ^ (uint8_t)entry.command[i]) * 16777619u;
    }

    return hash;
}
