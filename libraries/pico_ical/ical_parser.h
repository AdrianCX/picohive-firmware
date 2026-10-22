/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// ical_parser.h
//
// A tiny, streaming iCalendar (RFC 5545) parser that extracts events falling inside a requested time window and expands recurring events (RRULE).
//
// Features:
// - No exceptions, no STL, no dynamic allocation.
// - All storage is fixed-size and lives inside the parser object
// - - one 4096-byte line-assembly buffer (handles chunk boundaries + folding)
// - - an event array of at most ICAL_MAX_EVENTS entries (general_config.h, default 50)
// - - recurrence-expansion scratch buffers 
// - - Input is pushed in via feed() in chunks of at most 1024 bytes. The parser consumes what it can and keeps any partial logical line in its buffer.
//
// Timezone model:
// - The caller supplies a single UTC offset (seconds) for its local zone via begin()
// - Times written as UTC (trailing 'Z') are converted with that offset.
// - Times written as floating / TZID local wall-clock are assumed to already be in the caller's local zone.
//   This is exact for calendars whose TZID matches the viewer's zone (the common case) and avoids shipping a tz database.
// - DST transitions inside the window are not modelled.

#ifndef ICAL_PARSER_H
#define ICAL_PARSER_H

#include <cstdint>

#if __has_include("general_config.h")
#include "general_config.h"
#endif

#ifndef ICAL_MAX_EVENTS
#define ICAL_MAX_EVENTS 50   // static event list capacity, 144 bytes each
#endif

enum {
    ICAL_LINE_CAP   = 4096,  // static internal line buffer
    ICAL_TITLE_CAP  = 128,   // max title bytes (incl. terminating NUL)
    ICAL_MAX_EXDATE = 32,    // per-event EXDATE exclusions we track
    ICAL_MAX_BYDAY  = 16,    // BYDAY entries per RRULE
    ICAL_MAX_BYN    = 16,    // BYMONTHDAY / BYSETPOS entries per RRULE
    ICAL_MAX_MDAYS  = 40,    // candidate days collected for one month
    ICAL_MAX_CAND   = ICAL_MAX_MDAYS * 12 // a YEARLY period spans up to 12 months
};

// A single (possibly expanded) event occurrence, in the caller's local zone.
struct IcalEvent {
    char    title[ICAL_TITLE_CAP]; // NUL-terminated, truncated if longer
    int64_t startUtc;              // seconds since 1970-01-01 UTC
    int16_t year;                  // local wall-clock fields:
    int8_t  month;                 // 1..12
    int8_t  day;                   // 1..31
    int8_t  hour;                  // 0..23
    int8_t  minute;                // 0..59
    int8_t  weekday;               // 0=Sunday .. 6=Saturday
    int8_t  allDay;                // 1 if a DATE (no time) event
};

class IcalParser {
public:
    IcalParser();

    // Start a parse. rangeStartUtc/rangeEndUtc define window for events we want in UTC
    // tzOffsetSeconds is local-minus-UTC (e.g. -4h DST = -14400)
    void begin(int64_t rangeStartUtc, int64_t rangeEndUtc, int32_t tzOffsetSeconds);

    // Feed a chunk, partial trailing lines are retained across calls.
    void feed(const char* data, int32_t len);

    // Finish parsing: flush any buffered final line, then sort results.
    void end();

    // Results (valid after end()).
    int32_t          count() const { return eventCount_; }
    const IcalEvent& at(int32_t i) const { return events_[i]; }

    // Weekday of the window start, in the caller's local zone, 0=Sunday .. 6=Saturday
    int32_t getRangeStartWeekDay() const;

    void getRangeStartCivil(int& y, int& m, int& d, int& wkday) const;

private:
    // ---- streaming line assembly (RFC5545 unfolding) ----
    char    line_[ICAL_LINE_CAP];
    int32_t lineLen_;
    bool    pendingNewline_;
    bool    lineTruncated_;

    void processByte(char c);
    void commitLine();

    int64_t rangeStart_;
    int64_t rangeEnd_;
    int32_t tzOffset_;

    // ---- results ----
    IcalEvent events_[ICAL_MAX_EVENTS];
    int32_t   eventCount_;

    // ---- current VEVENT being assembled ----
    bool     inEvent_;
    bool     haveStart_;
    char     curTitle_[ICAL_TITLE_CAP];
    int64_t  startDays_;   // days since epoch of DTSTART wall date
    int32_t  startSod_;    // seconds-of-day of DTSTART wall time
    bool     startIsUtc_;  // DTSTART carried a trailing 'Z'
    bool     startIsDate_; // DTSTART was VALUE=DATE (all day)
    // RRULE
    bool     hasRrule_;
    int8_t   freq_;        // 0=none 1=DAILY 2=WEEKLY 3=MONTHLY 4=YEARLY
    int32_t  interval_;
    int32_t  count_;       // COUNT, 0 = unbounded
    int64_t  untilUtc_;    // UNTIL as utc epoch, INT64_MAX = none
    // BYDAY: each entry is (ordinal, iso-weekday). ord 0 = "every such weekday".
    // ord 3 = 3rd, ord -1 = last. Used by WEEKLY/MONTHLY/YEARLY.
    int8_t   byDayOrd_[ICAL_MAX_BYDAY];
    int8_t   byDayWd_[ICAL_MAX_BYDAY]; // iso Mon=0..Sun=6
    int8_t   byDayCount_;
    int8_t   byMonthDay_[ICAL_MAX_BYN]; // day-of-month, negative counts from end
    int8_t   byMonthDayCount_;
    int8_t   byMonth_[12];              // 1..12 restriction for YEARLY
    int8_t   byMonthCount_;
    int8_t   bySetPos_[ICAL_MAX_BYN];   // 1-based, negative from end of period set
    int8_t   bySetPosCount_;
    // EXDATE exclusions (utc epoch of each excluded instance)
    int64_t  exdates_[ICAL_MAX_EXDATE];
    int32_t  exdateCount_;

    void resetEvent();
    void parseDtStart(const char* nameAndParams, const char* value);
    void parseRrule(const char* value);
    void parseExdate(const char* nameAndParams, const char* value);
    void finalizeEvent();          // called at END:VEVENT: expand + emit

    int64_t toUtc(int64_t days, int32_t sod) const; // wall -> utc epoch
    bool    isExcluded(int64_t utc) const;
    void    emit(int64_t days, int32_t sod);        // add one occurrence if in window
    void    addEvent(int64_t utc);

    // ---- recurrence expansion scratch ----
    int64_t         cand_[ICAL_MAX_CAND];   // candidate days of the current period
    mutable int64_t picked_[ICAL_MAX_MDAYS];// applyBySetPos() selection

    // Recurrence expansion helpers.
    int32_t collectMonthDays(int y, int m, int64_t* out, int32_t cap, bool useDefault) const;
    void    applyBySetPos(int64_t* days, int32_t& n) const;
    // Emit a period's sorted candidate day list; returns true to stop (COUNT/UNTIL hit).
    bool    emitPeriod(int64_t* days, int32_t n, int32_t sod, int32_t& seq);
};

#endif
