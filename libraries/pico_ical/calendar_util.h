/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

typedef enum {
    TZ_UTC = 0,
    TZ_US_HAWAII,        /* UTC-10, no DST                         */
    TZ_US_ALASKA,        /* UTC-9,  US DST                         */
    TZ_US_PACIFIC,       /* UTC-8,  US DST                         */
    TZ_US_ARIZONA,       /* UTC-7,  no DST                         */
    TZ_US_MOUNTAIN,      /* UTC-7,  US DST                         */
    TZ_US_CENTRAL,       /* UTC-6,  US DST                         */
    TZ_US_EASTERN,       /* UTC-5,  US DST                         */
    TZ_ARGENTINA,        /* UTC-3,  no DST                         */
    TZ_BRAZIL_SAO_PAULO, /* UTC-3,  no DST (abolished 2019)        */
    TZ_UK,               /* UTC+0,  EU DST                         */
    TZ_CENTRAL_EUROPE,   /* UTC+1,  EU DST (Paris, Berlin, Madrid) */
    TZ_EASTERN_EUROPE,   /* UTC+2,  EU DST (Athens, Helsinki)      */
    TZ_TURKEY,           /* UTC+3,  no DST (since 2016)            */
    TZ_INDIA,            /* UTC+5:30, no DST                       */
    TZ_CHINA,            /* UTC+8,  no DST                         */
    TZ_JAPAN,            /* UTC+9,  no DST                         */
    TZ_AUSTRALIA_EAST,   /* UTC+10, southern DST (Sydney)          */
    TZ_NEW_ZEALAND,      /* UTC+12, southern DST                   */
    TZ_COUNT
} Timezone;

int64_t httpDateToUtc(const char* http_date);
int32_t convertTimezone(const char* http_date);
int32_t timezoneOffset(Timezone tz, int64_t utc_epoch);
int64_t httpDateToLocalEpoch(const char* http_date, Timezone tz);
void    formatEpoch(int64_t epoch, char* out);  // out must be more then 20 bytes
int64_t getStartDayEpochUtc(int64_t sourceEpochUtc);