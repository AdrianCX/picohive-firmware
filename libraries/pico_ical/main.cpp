/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Usage: ical <file.ics> <YYYY-MM-DD> <tz_offset_hours>
//   Prints every event in the local week (Mon..Sun) containing the given date.
//   tz_offset_hours: local minus UTC, e.g. -4 for EDT, 2 for CEST.

#include "ical_parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int64_t daysFromCivil(int y, int m, int d) {
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}
static void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)(yoe + era * 400);
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}
static int weekdayFromDays(int64_t z) { // 0=Sunday..6=Saturday
    return (int)((z >= -4) ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <file.ics> <YYYY-MM-DD> <tz_offset_hours>\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    int y, m, d;
    if (std::sscanf(argv[2], "%d-%d-%d", &y, &m, &d) != 3) {
        std::fprintf(stderr, "bad date\n"); return 2;
    }
    int32_t tz = (int32_t)(std::atof(argv[3]) * 3600.0);

    // Window: the start date 00:00 .. +7 days 00:00 (today + next 6), local -> UTC.
    int64_t day0 = daysFromCivil(y, m, d);
    int64_t rangeStart = day0 * 86400 - tz;          // -> UTC
    int64_t rangeEnd   = (day0 + 7) * 86400 - tz;

    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return 1; }

    IcalParser parser;
    parser.begin(rangeStart, rangeEnd, tz);

    char chunk[1024];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        parser.feed(chunk, (int32_t)n);
    std::fclose(f);
    parser.end();

    static const char* WD[] = {"Sunday","Monday","Tuesday","Wednesday",
                               "Thursday","Friday","Saturday"};
    std::printf("Week from %04d-%02d-%02d (7 days), %d event(s):\n\n",
                y, m, d, parser.count());
    // Walk the 7 consecutive dates in order, matching events by actual date.
    for (int off = 0; off < 7; ++off) {
        int cy, cm, cd;
        civilFromDays(day0 + off, cy, cm, cd);
        int cwd = weekdayFromDays(day0 + off);
        std::printf("%s %04d-%02d-%02d\n", WD[cwd], cy, cm, cd);
        bool any = false;
        for (int i = 0; i < parser.count(); ++i) {
            const IcalEvent& e = parser.at(i);
            if (e.year != cy || e.month != cm || e.day != cd) continue;
            any = true;
            if (e.allDay)
                std::printf("  (all day)  %s\n", e.title);
            else
                std::printf("  %02d:%02d       %s\n", e.hour, e.minute, e.title);
        }
        if (!any) std::printf("  -\n");
    }
    return 0;
}
