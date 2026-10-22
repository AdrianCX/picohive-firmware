/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>

#include "calendar_commands.h"
#include "calendar_status.h"

#ifndef CALENDAR_STATUS_SHOWN
#define CALENDAR_STATUS_SHOWN 5
#endif

// "Mon 2026-10-05 11:21" for a local epoch.
static void format_local(int64_t local, char *out, uint32_t size)
{
    static const char *WEEKDAY[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };

    int64_t days = local / 86400;
    int64_t seconds = local - (days * 86400);

    if (seconds < 0)
    {
        seconds += 86400;
        days -= 1;
    }

    // Days to civil date, Howard Hinnant's algorithm.
    int64_t z = days + 719468;
    int64_t era = ((z >= 0) ? z : (z - 146096)) / 146097;
    int64_t doe = z - (era * 146097);
    int64_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    int64_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    int64_t mp = ((5 * doy) + 2) / 153;
    int day = (int)(doy - (((153 * mp) + 2) / 5) + 1);
    int month = (int)((mp < 10) ? (mp + 3) : (mp - 9));
    int year = (int)((yoe + (era * 400)) + ((month <= 2) ? 1 : 0));

    // 1970-01-01 was a Thursday.
    int weekday = (int)(((days % 7) + 7) % 7);

    snprintf(out, size, "%s %04d-%02d-%02d %02d:%02d", WEEKDAY[weekday], year, month, day,
             (int)(seconds / 3600), (int)((seconds % 3600) / 60));
}

bool CalendarStatus::on_header(CommandContext& ctx)
{
    CalendarCommands& calendar = CalendarCommands::get();
    const CalendarCommandsConfig& config = calendar.get_config();

    if (!calendar.is_configured())
    {
        return ctx.reply("no calendar address or device id set");
    }

    ReplyBuffer text(ctx, 1024);
    if (!text)
    {
        return ctx.reply("no memory for the reply");
    }

    uint64_t now_us = calendar.now_us();

    if (calendar.is_fetching() || (calendar.next_fetch_us() <= now_us))
    {
        text.append(config.id, ": fetching now");
    }
    else
    {
        uint64_t next_m = (calendar.next_fetch_us() - now_us + 59999999ULL) / 60000000ULL;

        if (calendar.last_fetch_ok())
        {
            text.append_snprintf("%s: fetched %llum ago, next in %llum", config.id,
                                 (unsigned long long)((now_us - calendar.last_fetch_us()) / 60000000ULL), (unsigned long long)next_m);
        }
        else
        {
            text.append_snprintf("%s: fetch failed: %s, next try in %llum", config.id,
                                 calendar.last_error(), (unsigned long long)next_m);
        }
    }

    text.append(", allowed [", config.allowed, "]");

    if (!calendar.clock_known())
    {
        text.append("\nno time yet, nothing runs until a fetch works");
        return ctx.reply(text);
    }

    const CalendarSchedule& schedule = calendar.schedule();
    int64_t now = calendar.now_utc();
    uint32_t coming = 0;

    char board_time[32] = {};
    format_local(now + calendar.tz_offset(), board_time, sizeof(board_time));

    int32_t offset_m = calendar.tz_offset() / 60;
    text.append_snprintf("\nnow\t%s UTC%c%d", board_time, (offset_m < 0) ? '-' : '+', abs(offset_m) / 60);

    if ((abs(offset_m) % 60) != 0)
    {
        text.append_snprintf(":%02d", abs(offset_m) % 60);
    }

    for (uint32_t i = 0; (i < schedule.count()) && (coming < CALENDAR_STATUS_SHOWN); ++i)
    {
        if (schedule.start_at(i) <= now)
        {
            continue;
        }

        char when[32] = {};
        format_local(schedule.start_at(i) + calendar.tz_offset(), when, sizeof(when));

        std::string_view command = schedule.command_at(i);
        text.append_snprintf("\n%s\t%.*s%s", when, (int)command.size(), command.data(),
                             calendar_allowed(config.allowed, command) ? "" : "\tnot allowed");
        ++coming;
    }

    if (coming == 0)
    {
        text.append_snprintf("\nnothing coming up in the next %u days", (unsigned int)(CALENDAR_LOOKAHEAD_S / 86400));
    }

    return ctx.reply(text);
}

MQTT_COMMAND(CalendarStatus);
WS_COMMAND(CalendarStatus);
TELEGRAM_COMMAND(CalendarStatus);
