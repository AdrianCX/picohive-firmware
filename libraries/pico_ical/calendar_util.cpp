/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdint.h>
#include "calendar_util.h"

static int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int yy       = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned mp  = (5*doy + 2) / 153;
    unsigned dd  = doy - (153*mp + 2)/5 + 1;
    unsigned mm  = mp + (mp < 10 ? 3 : (unsigned)-9);
    *y = yy + (mm <= 2);
    *m = mm;
    *d = dd;
}

static int weekday_from_days(int64_t z)
{
    int w = (int)((z + 4) % 7);
    return w < 0 ? w + 7 : w;
}

static int nth_weekday(int year, int month, int wday, int nth)
{
    int fw = weekday_from_days(days_from_civil(year, month, 1));
    return 1 + ((wday - fw + 7) % 7) + (nth - 1) * 7;
}

static int last_weekday(int year, int month, int wday)
{
    int ny = year, nm = month + 1;
    if (nm > 12) { nm = 1; ny++; }
    int64_t last = days_from_civil(ny, nm, 1) - 1;
    int lw  = weekday_from_days(last);
    int dom = (int)(last - days_from_civil(year, month, 1)) + 1;
    return dom - ((lw - wday + 7) % 7);
}

static int64_t parseCivilEpoch(const char* p)
{
    if (!p) return INT64_MIN;

    while (*p && *p != ',') p++;
    if (*p != ',') return INT64_MIN;
    p++;
    if (*p != ' ') return INT64_MIN;
    p++;

    int day = 0;
    for (int i = 0; i < 2; i++)
	{
        if (*p == ' ' && i == 0) { p++; continue; }

        if (*p < '0' || *p > '9') return INT64_MIN;
		
        day = day * 10 + (*p - '0');
        p++;
    }
    if (*p != ' ') return INT64_MIN;
    p++;

    static const char names[12][3] = {
        {'J','a','n'},{'F','e','b'},{'M','a','r'},{'A','p','r'},
        {'M','a','y'},{'J','u','n'},{'J','u','l'},{'A','u','g'},
        {'S','e','p'},{'O','c','t'},{'N','o','v'},{'D','e','c'}
    };
    int month = 0;
    for (int i = 0; i < 12; i++)
	{
        if (p[0]==names[i][0] && p[1]==names[i][1] && p[2]==names[i][2])
		{
            month = i + 1; break;
        }
    }
    if (month == 0) return INT64_MIN;
    p += 3;
    if (*p != ' ') return INT64_MIN;
    p++;

    int year = 0;
    for (int i = 0; i < 4; i++)
	{
        if (*p < '0' || *p > '9') return INT64_MIN;
        year = year * 10 + (*p - '0');
        p++;
    }
    if (*p != ' ') return INT64_MIN;
    p++;

    int hms[3] = {0,0,0};
    for (int f = 0; f < 3; f++)
	{
        for (int i = 0; i < 2; i++)
		{
            if (*p < '0' || *p > '9') return INT64_MIN;
            hms[f] = hms[f]*10 + (*p - '0');
            p++;
        }
        if (f < 2)
		{
			if (*p != ':')
			{
				return INT64_MIN;
			}
			p++;
		}
    }
	
    int hour = hms[0], min = hms[1], sec = hms[2];

    if (day < 1 || day > 31 || month < 1 || month > 12 || hour > 23 || min > 59 || sec > 60)
	{
        return INT64_MIN;
    }

    int64_t days = days_from_civil(year, month, day);
    return days * 86400 + (int64_t)hour*3600 + (int64_t)min*60 + sec;
}

int32_t convertTimezone(const char* http_date)
{
    const char* end = http_date;
    while (*end) end++;

    const char* q = end;
    while (q > http_date && (q[-1] == ' ' || q[-1] == '\t')) q--;

    const char* start = q;
    while (start > http_date && start[-1] != ' ' && start[-1] != '\t') start--;

    const char* p = start;
    int len = (int)(q - start);
    if (len <= 0) return INT32_MIN;

    if (*p == '+' || *p == '-')
	{
        if (len != 5) return INT32_MIN;
        int sign = (*p == '-') ? -1 : 1;
        p++;
        for (int i = 0; i < 4; i++)
		{
            if (p[i] < '0' || p[i] > '9') return INT32_MIN;
		}

		int hh = (p[0]-'0')*10 + (p[1]-'0');
        int mm = (p[2]-'0')*10 + (p[3]-'0');
		
        if (hh > 23 || mm > 59)
		{
			return INT32_MIN;
		}
        return sign * (hh*3600 + mm*60);
    }

    static const struct { char name[5]; int32_t off; } zones[] = {
        {"GMT",0},{"UTC",0},{"UT",0},{"Z",0},
        {"EST",-5*3600},{"EDT",-4*3600},{"CST",-6*3600},{"CDT",-5*3600},
        {"MST",-7*3600},{"MDT",-6*3600},{"PST",-8*3600},{"PDT",-7*3600},
    };
    for (int i = 0; i < (int)(sizeof(zones)/sizeof(zones[0])); i++)
	{
        const char* z = zones[i].name;
        int j = 0;
        while (j < len && z[j] && p[j] == z[j]) j++;
        if (j == len && z[j] == '\0') return zones[i].off;
    }
    return INT32_MIN;
}

int64_t httpDateToUtc(const char* http_date) {
    int64_t civil = parseCivilEpoch(http_date);
    if (civil == INT64_MIN) return INT64_MIN;
    int32_t src = convertTimezone(http_date);
    if (src == INT32_MIN) return INT64_MIN;
    return civil - src;
}

enum { DST_NONE, DST_US, DST_EU, DST_AU, DST_NZ };

static int32_t dst_offset(int rule, int64_t utc, int32_t std_off)
{
    if (rule == DST_NONE) return 0;
    int y; unsigned mo, d;
    civil_from_days(utc / 86400 - (utc % 86400 < 0 ? 1 : 0), &y, &mo, &d);
    const int SUN = 0;

    if (rule == DST_US)
	{
        int64_t s = days_from_civil(y,3, nth_weekday(y,3,SUN,2))*86400 + 2*3600 - std_off;
        int64_t e = days_from_civil(y,11,nth_weekday(y,11,SUN,1))*86400 + 2*3600 - (std_off+3600);
        return (utc >= s && utc < e) ? 3600 : 0;
    }
    if (rule == DST_EU)
	{
        int64_t s = days_from_civil(y,3, last_weekday(y,3,SUN))*86400 + 3600;
        int64_t e = days_from_civil(y,10,last_weekday(y,10,SUN))*86400 + 3600;
        return (utc >= s && utc < e) ? 3600 : 0;
    }
    if (rule == DST_AU)
	{
        int64_t s = days_from_civil(y,10,nth_weekday(y,10,SUN,1))*86400 + 2*3600 - std_off;
        int64_t e = days_from_civil(y,4, nth_weekday(y,4,SUN,1))*86400 + 3*3600 - (std_off+3600);
        return (utc >= s || utc < e) ? 3600 : 0;
    }
    if (rule == DST_NZ)
	{
        int64_t s = days_from_civil(y,9, last_weekday(y,9,SUN))*86400 + 2*3600 - std_off;
        int64_t e = days_from_civil(y,4, nth_weekday(y,4,SUN,1))*86400 + 3*3600 - (std_off+3600);
        return (utc >= s || utc < e) ? 3600 : 0;
    }
    return 0;
}

int32_t timezoneOffset(Timezone tz, int64_t utc_epoch)
{
    static const struct { int32_t std; int rule; } T[TZ_COUNT] = {
        [TZ_UTC]              = {         0, DST_NONE },
        [TZ_US_HAWAII]        = { -10*3600, DST_NONE },
        [TZ_US_ALASKA]        = {  -9*3600, DST_US   },
        [TZ_US_PACIFIC]       = {  -8*3600, DST_US   },
        [TZ_US_ARIZONA]       = {  -7*3600, DST_NONE },
        [TZ_US_MOUNTAIN]      = {  -7*3600, DST_US   },
        [TZ_US_CENTRAL]       = {  -6*3600, DST_US   },
        [TZ_US_EASTERN]       = {  -5*3600, DST_US   },
        [TZ_ARGENTINA]        = {  -3*3600, DST_NONE },
        [TZ_BRAZIL_SAO_PAULO] = {  -3*3600, DST_NONE },
        [TZ_UK]               = {         0, DST_EU   },
        [TZ_CENTRAL_EUROPE]   = {   1*3600, DST_EU   },
        [TZ_EASTERN_EUROPE]   = {   2*3600, DST_EU   },
        [TZ_TURKEY]           = {   3*3600, DST_NONE },
        [TZ_INDIA]            = {   5*3600 + 1800, DST_NONE },
        [TZ_CHINA]            = {   8*3600, DST_NONE },
        [TZ_JAPAN]            = {   9*3600, DST_NONE },
        [TZ_AUSTRALIA_EAST]   = {  10*3600, DST_AU   },
        [TZ_NEW_ZEALAND]      = {  12*3600, DST_NZ   },
    };

    if ((unsigned)tz >= (unsigned)TZ_COUNT)
	{
		return INT32_MIN;
	}
    int32_t std = T[tz].std;
	
    return std + dst_offset(T[tz].rule, utc_epoch, std);
}

int64_t httpDateToLocalEpoch(const char* http_date, Timezone tz)
{
    int64_t utc = httpDateToUtc(http_date);
    if (utc == INT64_MIN) return INT64_MIN;
    int32_t off = timezoneOffset(tz, utc);
    if (off == INT32_MIN) return INT64_MIN;
    return utc + off;
}

int64_t getStartDayEpochUtc(int64_t sourceEpochUtc)
{
    int64_t rem = sourceEpochUtc % 86400;
    if (rem < 0) rem += 86400;
    return sourceEpochUtc - rem;
}

void formatEpoch(int64_t epoch, char* out)
{
    int64_t days = epoch / 86400;
    int64_t rem  = epoch % 86400;
    if (rem < 0)
	{
		rem += 86400; days -= 1;
	}

    int y; unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);

    int hh = (int)(rem / 3600);
    int mi = (int)((rem % 3600) / 60);
    int ss = (int)(rem % 60);

    int yy = y; if (yy < 0) yy = 0; if (yy > 9999) yy = 9999;

    out[0]='0'+(yy/1000)%10; out[1]='0'+(yy/100)%10;
    out[2]='0'+(yy/10)%10;   out[3]='0'+yy%10;
    out[4]=':'; out[5]='0'+(mo/10)%10; out[6]='0'+mo%10;
    out[7]=':'; out[8]='0'+(d /10)%10; out[9]='0'+d %10;
    out[10]=' ';out[11]='0'+(hh/10)%10;out[12]='0'+hh%10;
    out[13]=':';out[14]='0'+(mi/10)%10;out[15]='0'+mi%10;
    out[16]=':';out[17]='0'+(ss/10)%10;out[18]='0'+ss%10;
    out[19]='\0';
}
