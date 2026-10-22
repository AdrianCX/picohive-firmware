/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "ical_parser.h"
#include <cstring>

namespace
{

int64_t daysFromCivil(int y, int m, int d)
{
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int32_t yoe = (int32_t)(y - era * 400);
    const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int32_t doe = (int32_t)(z - era * 146097);
    const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)(yoe + era * 400);
    const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int32_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}

int weekdayFromDays(int64_t z)
{
    return (int)((z >= -4) ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

int isoWeekday(int64_t z)
{
	return (weekdayFromDays(z) + 6) % 7;
}

int lastDayOfMonth(int y, int m)
{
    static const int8_t md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2)
	{
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return md[m - 1];
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int parseInt(const char* s, int n)
{
    int v = 0;
    for (int i = 0; i < n; ++i) v = v * 10 + (s[i] - '0');
    return v;
}

int parseSigned(const char* s, const char* end, int* consumed)
{
    const char* p = s;
    int sign = 1;

	if (p < end && (*p == '+' || *p == '-'))
	{
		if (*p == '-')
		{
			sign = -1;
		}
		++p;
	}

    int v = 0;
	
    while (p < end && isDigit(*p))
	{
		v = v * 10 + (*p - '0');
		++p;
	}
    *consumed = (int)(p - s);
    return sign * v;
}

int64_t nthWeekdayOfMonth(int y, int m, int wdIso, int ord)
{
    int64_t first = daysFromCivil(y, m, 1);
    int dim = lastDayOfMonth(y, m);
    int firstIso = isoWeekday(first);
    int firstDate = 1 + (wdIso - firstIso + 7) % 7;

	if (ord > 0)
	{
        int date = firstDate + (ord - 1) * 7;

		if (date > dim)
		{
			return -1;
		}
		
        return daysFromCivil(y, m, date);
    }

    int last = firstDate;
    while (last + 7 <= dim)
	{
		last += 7;
	}
	
    int date = last + (ord + 1) * 7;
    if (date < 1)
	{
		return -1;
	}
	
    return daysFromCivil(y, m, date);
}

void sortDays(int64_t* a, int32_t n)
{
    for (int32_t i = 1; i < n; ++i)
	{
        int64_t t = a[i];
        int32_t j = i - 1;
        while (j >= 0 && a[j] > t)
		{
			a[j + 1] = a[j];
			--j;
		}
        a[j + 1] = t;
    }
}

bool startsWith(const char* s, const char* pfx)
{
    while (*pfx) {
        char a = *s++, b = *pfx++;
		
        if (a >= 'a' && a <= 'z')
		{
			a = (char)(a - 32);
		}
		
        if (b >= 'a' && b <= 'z')
		{
			b = (char)(b - 32);
		}
		
        if (a != b)
		{
			return false;
		}
    }
    return true;
}

bool parseDateValue(const char* v, int64_t& days, int32_t& sod, bool& isUtc, bool& isDate)
{
    int len = (int)strlen(v);
    if (len < 8)
	{
		return false;
	}
	
    for (int i = 0; i < 8; ++i)
	{
		if (!isDigit(v[i]))
		{
			return false;
		}
	}
	
    int y = parseInt(v, 4), mo = parseInt(v + 4, 2), d = parseInt(v + 6, 2);
	
    if (mo < 1 || mo > 12 || d < 1 || d > 31)
	{
		return false;
	}
	
    days = daysFromCivil(y, mo, d);
    sod = 0;
    isUtc = false;
    isDate = true;
	
    if (len >= 15 && (v[8] == 'T' || v[8] == 't'))
	{
        int hh = parseInt(v + 9, 2), mm = parseInt(v + 11, 2), ss = parseInt(v + 13, 2);
        sod = hh * 3600 + mm * 60 + ss;
        isDate = false;
        isUtc = (len >= 16 && (v[15] == 'Z' || v[15] == 'z'));
    }
    return true;
}

}

IcalParser::IcalParser()
{
	begin(0, 0, 0);
}

void IcalParser::begin(int64_t rangeStartUtc, int64_t rangeEndUtc, int32_t tzOffsetSeconds)
{
    lineLen_ = 0;
    pendingNewline_ = false;
    lineTruncated_ = false;
    rangeStart_ = rangeStartUtc;
    rangeEnd_ = rangeEndUtc;
    tzOffset_ = tzOffsetSeconds;
    eventCount_ = 0;
    inEvent_ = false;
    resetEvent();
}

void IcalParser::resetEvent()
{
    haveStart_ = false;
    curTitle_[0] = '\0';
    startDays_ = 0;
    startSod_ = 0;
    startIsUtc_ = false;
    startIsDate_ = false;
    hasRrule_ = false;
    freq_ = 0;
    interval_ = 1;
    count_ = 0;
    untilUtc_ = INT64_MAX;
    byDayCount_ = 0;
    byMonthDayCount_ = 0;
    byMonthCount_ = 0;
    bySetPosCount_ = 0;
    exdateCount_ = 0;
}

void IcalParser::feed(const char* data, int32_t len)
{
    for (int32_t i = 0; i < len; ++i)
	{
		processByte(data[i]);
	}
}

void IcalParser::end()
{
    if (pendingNewline_ || lineLen_ > 0)
	{
		commitLine();
	}

    for (int32_t i = 1; i < eventCount_; ++i)
	{
        IcalEvent tmp = events_[i];
        int32_t j = i - 1;
        while (j >= 0 && ((events_[j].startUtc > tmp.startUtc) || (events_[j].startUtc == tmp.startUtc && strcmp(events_[j].title, tmp.title) > 0)))
		{
            events_[j + 1] = events_[j];
            --j;
        }
        events_[j + 1] = tmp;
    }
}

void IcalParser::processByte(char c)
{
    if (pendingNewline_) {
        pendingNewline_ = false;
        if (c == ' ' || c == '\t')
		{
            return;
        }
        commitLine();
        lineLen_ = 0;
        lineTruncated_ = false;
    }

    if (c == '\r')
	{
		return;
	}
    if (c == '\n')
	{
		pendingNewline_ = true;
		return;
	}

    if (lineLen_ < ICAL_LINE_CAP - 1)
	{
        line_[lineLen_++] = c;
    }
	else
	{
        lineTruncated_ = true;
    }
}

void IcalParser::commitLine()
{
    if (lineTruncated_ || lineLen_ == 0)
	{
		return;
	}

    line_[lineLen_] = '\0';

    int colon = -1;
    for (int32_t i = 0; i < lineLen_; ++i)
	{
        if (line_[i] == ':')
		{
			colon = i;
			break;
		}
    }

    if (startsWith(line_, "BEGIN:VEVENT"))
	{
        inEvent_ = true;
        resetEvent();
        return;
    }
    if (startsWith(line_, "END:VEVENT"))
	{
        if (inEvent_)
		{
			finalizeEvent();
		}
		
        inEvent_ = false;
        return;
    }
	
    if (!inEvent_ || colon < 0)
	{
		return;
	}

    line_[colon] = '\0';
    const char* name = line_;
    const char* value = line_ + colon + 1;

    if (startsWith(name, "SUMMARY"))
	{
        int32_t n = 0;
        while (value[n] && n < ICAL_TITLE_CAP - 1)
		{
			curTitle_[n] = value[n];
			++n;
		}
		
        curTitle_[n] = '\0';
		
    }
	else if (startsWith(name, "DTSTART"))
	{
        parseDtStart(name, value);
    }
	else if (startsWith(name, "RRULE"))
	{
        parseRrule(value);
    }
	else if (startsWith(name, "EXDATE"))
	{
        parseExdate(name, value);
    }
}

void IcalParser::parseDtStart(const char* nameAndParams, const char* value)
{
    int64_t days = 0;
	int32_t sod = 0;
	bool isUtc = false;
	bool isDate = false;
	
    if (!parseDateValue(value, days, sod, isUtc, isDate))
	{
		return;
	}
	
    if (strstr(nameAndParams, "VALUE=DATE") || strstr(nameAndParams, "value=date"))
	{
        isDate = true;
	}

    startDays_ = days;
    startSod_ = sod;
    startIsUtc_ = isUtc;
    startIsDate_ = isDate;
    haveStart_ = true;
}

void IcalParser::parseRrule(const char* value)
{
    hasRrule_ = true;

    const char* p = value;
	
    while (*p)
	{
        const char* key = p;

		while (*p && *p != '=' && *p != ';')
		{
			++p;
		}
		
        int32_t klen = (int32_t)(p - key);
        const char* val = p;

		if (*p == '=')
		{
			++p;
			val = p;
			
			while (*p && *p != ';')
			{
				++p;
			}
		}
		
        int32_t vlen = (int32_t)(p - val);

        if (klen == 4 && startsWith(key, "FREQ"))
		{
            if (startsWith(val, "DAILY"))
			{
				freq_ = 1;
			}
            else if (startsWith(val, "WEEKLY"))
			{
				freq_ = 2;
			}
            else if (startsWith(val, "MONTHLY"))
			{
				freq_ = 3;
			}
            else if (startsWith(val, "YEARLY"))
			{
				freq_ = 4;
			}
        }
		else if (klen == 8 && startsWith(key, "INTERVAL"))
		{
            int v = 0;
			for (int32_t i = 0; i < vlen && isDigit(val[i]); ++i)
			{
				v = v * 10 + (val[i] - '0');
			}
            if (v > 0)
			{
				interval_ = v;
			}
        }
		else if (klen == 5 && startsWith(key, "COUNT"))
		{
            int v = 0;
			for (int32_t i = 0; i < vlen && isDigit(val[i]); ++i)
			{
				v = v * 10 + (val[i] - '0');
			}
            count_ = v;
        }
		else if (klen == 5 && startsWith(key, "UNTIL"))
		{
            char buf[24] = "";
			int32_t n = vlen < 23 ? vlen : 23;
            memcpy(buf, val, n); buf[n] = '\0';
            int64_t d = 0;
			int32_t s = 0;
			bool u = false;
			bool dt = false;
			
            if (parseDateValue(buf, d, s, u, dt))
			{
                int32_t untilSod = dt ? 86399 : s;
                untilUtc_ = u ? (d * 86400 + untilSod) : toUtc(d, untilSod);
            }
        }
		else if (klen == 5 && startsWith(key, "BYDAY"))
		{
            byDayCount_ = 0;
            const char* q = val;
            const char* stop = val + vlen;
            while (q < stop)
			{
                int used = 0;
                int ord = parseSigned(q, stop, &used);
                q += used;
                if (q + 1 < stop || q + 1 == stop)
				{
                    char a = q < stop ? q[0] : 0;
                    char b = (q + 1 < stop) ? q[1] : 0;
                    int idx = -1;
                    if (a == 'M' && b == 'O') idx = 0;
                    else if (a == 'T' && b == 'U') idx = 1;
                    else if (a == 'W' && b == 'E') idx = 2;
                    else if (a == 'T' && b == 'H') idx = 3;
                    else if (a == 'F' && b == 'R') idx = 4;
                    else if (a == 'S' && b == 'A') idx = 5;
                    else if (a == 'S' && b == 'U') idx = 6;
                    if (idx >= 0 && byDayCount_ < ICAL_MAX_BYDAY)
					{
                        byDayOrd_[byDayCount_] = (int8_t)ord;
                        byDayWd_[byDayCount_] = (int8_t)idx;
                        ++byDayCount_;
                    }
                }

				while (q < stop && *q != ',')
				{
					++q;
				}
				
                if (q < stop)
				{
					++q;
				}
            }
        }
		else if (klen == 10 && startsWith(key, "BYMONTHDAY"))
		{
            byMonthDayCount_ = 0;
            const char* q = val;
            const char* stop = val + vlen;
            while (q < stop)
			{
                int used = 0;
                int v = parseSigned(q, stop, &used);
                q += used;
				
                if (used > 0 && v != 0 && byMonthDayCount_ < ICAL_MAX_BYN)
				{
                    byMonthDay_[byMonthDayCount_++] = (int8_t)v;
				}
                while (q < stop && *q != ',')
				{
					++q;
				}
				
                if (q < stop)
				{
					++q;
				}
            }
        } else if (klen == 7 && startsWith(key, "BYMONTH"))
		{
            byMonthCount_ = 0;
            const char* q = val;
            const char* stop = val + vlen;

			while (q < stop)
			{
                int used = 0;
                int v = parseSigned(q, stop, &used);
                q += used;
                if (used > 0 && v >= 1 && v <= 12 && byMonthCount_ < 12)
				{
                    byMonth_[byMonthCount_++] = (int8_t)v;
				}
                while (q < stop && *q != ',')
				{
					++q;
				}
                if (q < stop)
				{
					++q;
				}
            }
        } else if (klen == 8 && startsWith(key, "BYSETPOS"))
		{
            bySetPosCount_ = 0;
            const char* q = val;
            const char* stop = val + vlen;
            while (q < stop)
			{
                int used = 0;
                int v = parseSigned(q, stop, &used);
                q += used;
                if (used > 0 && v != 0 && bySetPosCount_ < ICAL_MAX_BYN)
				{
                    bySetPos_[bySetPosCount_++] = (int8_t)v;
				}
                while (q < stop && *q != ',')
				{
					++q;
				}
                if (q < stop)
				{
					++q;
				}
            }
        }
        if (*p == ';')
		{
			++p;
		}
    }
}

void IcalParser::parseExdate(const char* /*nameAndParams*/, const char* value)
{
    const char* p = value;
    while (*p && exdateCount_ < ICAL_MAX_EXDATE)
	{
        char buf[24] = "";
        int32_t n = 0;
		
        while (*p && *p != ',' && n < 23)
		{
			buf[n++] = *p++;
		}
		
        buf[n] = '\0';
		
        int64_t d = 0;
		int32_t s = 0;
		bool u = false;
		bool dt = false;

		if (parseDateValue(buf, d, s, u, dt))
		{
            int64_t utc = u ? (d * 86400 + s) : toUtc(d, s);
            exdates_[exdateCount_++] = utc;
        }
        if (*p == ',')
		{
			++p;
		}
    }
}

int64_t IcalParser::toUtc(int64_t days, int32_t sod) const
{
    return days * 86400 + sod - tzOffset_;
}

bool IcalParser::isExcluded(int64_t utc) const
{
    for (int32_t i = 0; i < exdateCount_; ++i)
	{
        if (exdates_[i] == utc) return true;
	}
    return false;
}

void IcalParser::addEvent(int64_t utc)
{
    int32_t slot = eventCount_;
    if (eventCount_ >= ICAL_MAX_EVENTS)
	{
        slot = 0;
        for (int32_t i = 1; i < eventCount_; ++i)
		{
            if (events_[i].startUtc > events_[slot].startUtc) slot = i;
		}
        if (events_[slot].startUtc <= utc)
		{
			return;
		}
    }
    IcalEvent& e = events_[slot];

    int32_t n = 0;
    while (curTitle_[n] && n < ICAL_TITLE_CAP - 1)
	{
		e.title[n] = curTitle_[n];
		++n;
	}
    e.title[n] = '\0';
    e.startUtc = utc;
    e.allDay = startIsDate_ ? 1 : 0;

    int64_t localSecs = utc + tzOffset_;
    int64_t ldays = localSecs / 86400;
    int32_t lsod = (int32_t)(localSecs - ldays * 86400);
    if (lsod < 0)
	{
		lsod += 86400;
		ldays -= 1;
	}
    int y, mo, d;
    civilFromDays(ldays, y, mo, d);
    e.year = (int16_t)y; e.month = (int8_t)mo; e.day = (int8_t)d;
    e.hour = (int8_t)(lsod / 3600);
    e.minute = (int8_t)((lsod % 3600) / 60);
    e.weekday = (int8_t)weekdayFromDays(ldays);
    if (slot == eventCount_) ++eventCount_;
}

int32_t IcalParser::getRangeStartWeekDay() const
{
    int64_t localSecs = rangeStart_ + tzOffset_;
    int64_t ldays = localSecs / 86400;
    if (localSecs - ldays * 86400 < 0)
	{
		ldays -= 1;
	}
    return (int32_t)weekdayFromDays(ldays);
}

void IcalParser::getRangeStartCivil(int& y, int& m, int& d, int& wkday) const
{
    int64_t localSecs = rangeStart_ + tzOffset_;
    int64_t ldays = localSecs / 86400;
    if (localSecs - ldays * 86400 < 0)
	{
		ldays -= 1;
	}

    civilFromDays(ldays, y, m, d);
    wkday = weekdayFromDays(ldays);
}


void IcalParser::emit(int64_t days, int32_t sod)
{
    int64_t utc = startIsUtc_ ? (days * 86400 + sod) : toUtc(days, sod);
    if (utc < rangeStart_ || utc >= rangeEnd_)
	{
		return;
	}
    if (isExcluded(utc))
	{
		return;
	}
    addEvent(utc);
}

void IcalParser::finalizeEvent()
{
    if (!haveStart_)
	{
		return;
	}

    const int32_t sod = startSod_;

    if (!hasRrule_ || freq_ == 0)
	{
        emit(startDays_, sod);
        return;
    }

    const int32_t SAFETY = 200000;
    int32_t produced = 0;
    bool bounded = (count_ > 0);

    if (freq_ == 1)
	{
        int64_t k = 0;
        if (!bounded)
		{
            int64_t sDay = rangeStart_ / 86400 - 2;
            if (sDay > startDays_)
			{
                k = (sDay - startDays_) / interval_;
			}
            if (k < 0)
			{
				k = 0;
			}
        }
        for (int32_t iter = 0; iter < SAFETY; ++iter, ++k)
		{
            int64_t day = startDays_ + k * interval_;
            int64_t utc = startIsUtc_ ? (day * 86400 + sod) : toUtc(day, sod);
            if (utc > untilUtc_) break;
            if (bounded && (k) >= count_) break;
            if (utc >= rangeEnd_) break;
            emit(day, sod);
            ++produced;
            (void)produced;
        }
    }
	else if (freq_ == 2)
	{
        int64_t startMon = startDays_ - isoWeekday(startDays_);
        uint8_t mask = 0;
        for (int32_t i = 0; i < byDayCount_; ++i)
		{
			mask |= (uint8_t)(1 << byDayWd_[i]);
		}
		
        if (mask == 0)
		{
			mask = (uint8_t)(1 << isoWeekday(startDays_));
		}
		
        int64_t wk = 0;
		
        if (!bounded)
		{
            int64_t sMon = (rangeStart_ / 86400) - isoWeekday(rangeStart_ / 86400) - 7;
            if (sMon > startMon)
			{
                wk = (sMon - startMon) / (7 * interval_);
			}
            if (wk < 0)
			{
				wk = 0;
			}
        }
		
        int32_t seq = 0;

		for (int32_t iter = 0; iter < SAFETY; ++iter, ++wk)
		{
            int64_t weekStart = startMon + wk * (int64_t)interval_ * 7;
            bool anyFuture = false;
			
            for (int dd = 0; dd < 7; ++dd)
			{
                if (!(mask & (1 << dd)))
				{
					continue;
				}
				
                int64_t day = weekStart + dd;
                if (day < startDays_)
				{
					continue;
				}
				
                anyFuture = true;
                int64_t utc = startIsUtc_ ? (day * 86400 + sod) : toUtc(day, sod);

				if (utc > untilUtc_)
				{
					anyFuture = false;
					break;
				}

				if (bounded && seq >= count_)
				{
					anyFuture = false;
					break;
				}
				
                ++seq;
				
                if (utc < rangeEnd_)
				{
					emit(day, sod);
				}
            }
            if (weekStart > rangeEnd_ / 86400 + 7 && !anyFuture)
			{
				break;
			}

            int64_t wkUtc = startIsUtc_ ? (weekStart * 86400 + sod) : toUtc(weekStart, sod);

			if (wkUtc >= rangeEnd_)
			{
				break;
			}
            if (bounded && seq >= count_)
			{
				break;
			}
			
            if (wkUtc > untilUtc_)
			{
				break;
			}
        }
    }
	else if (freq_ == 3)
	{
        int sy = 0;
		int sm = 0;
		int sd = 0;
        civilFromDays(startDays_, sy, sm, sd);
		
        int32_t seq = 0;
		
        for (int32_t k = 0; k < SAFETY; ++k)
		{
            int64_t total = (int64_t)sm - 1 + (int64_t)k * interval_;
            int y = sy + (int)(total / 12);
            int m = (int)(total % 12) + 1;

            int64_t monthStart = daysFromCivil(y, m, 1);

			if ((startIsUtc_ ? monthStart * 86400 + sod : toUtc(monthStart, sod)) >= rangeEnd_)
			{
                break;
			}

			int32_t n = collectMonthDays(y, m, cand_, ICAL_MAX_MDAYS, /*useDefault=*/true);

			applyBySetPos(cand_, n);

			if (emitPeriod(cand_, n, sod, seq))
			{
				break;
			}
        }
    } else if (freq_ == 4) {
        int sy = 0;
		int sm = 0;
		int sd = 0;
        civilFromDays(startDays_, sy, sm, sd);
		
        int32_t seq = 0;
        for (int32_t k = 0; k < SAFETY; ++k)
		{
            int y = sy + (int)((int64_t)k * interval_);
            int64_t yearStart = daysFromCivil(y, 1, 1);
			
            if ((startIsUtc_ ? yearStart * 86400 + sod : toUtc(yearStart, sod)) >= rangeEnd_)
			{
                break;
			}
			
            int32_t n = 0;
            if (byMonthCount_ > 0)
			{
                for (int32_t i = 0; i < byMonthCount_; ++i)
				{
                    n += collectMonthDays(y, byMonth_[i], cand_ + n, ICAL_MAX_CAND - n, true);
				}
            }
			else
			{
                n = collectMonthDays(y, sm, cand_, ICAL_MAX_MDAYS, true);
            }
            sortDays(cand_, n);
            applyBySetPos(cand_, n);
            if (emitPeriod(cand_, n, sod, seq))
			{
				break;
			}
        }
    }
}

int32_t IcalParser::collectMonthDays(int y, int m, int64_t* out, int32_t cap, bool useDefault) const
{
    int32_t n = 0;
    int dim = lastDayOfMonth(y, m);
    if (byDayCount_ > 0)
	{
        for (int32_t i = 0; i < byDayCount_ && n < cap; ++i)
		{
            int wd = byDayWd_[i];
			int ord = byDayOrd_[i];
			
            if (ord == 0)
			{
                int64_t first = daysFromCivil(y, m, 1);
                int off = (wd - isoWeekday(first) + 7) % 7;
                for (int date = 1 + off; date <= dim && n < cap; date += 7)
				{
                    out[n++] = daysFromCivil(y, m, date);
				}
            }
			else
			{
                int64_t d = nthWeekdayOfMonth(y, m, wd, ord);
                if (d >= 0)
				{
					out[n++] = d;
				}
            }
        }
    }
	else if (byMonthDayCount_ > 0)
	{
        for (int32_t i = 0; i < byMonthDayCount_ && n < cap; ++i)
		{
            int md = byMonthDay_[i];
            int day = md > 0 ? md : dim + 1 + md;
            if (day >= 1 && day <= dim)
			{
				out[n++] = daysFromCivil(y, m, day);
			}
        }
    }
	else if (useDefault)
	{
        int sy = 0;
		int sm = 0;
		int sd = 0;
        civilFromDays(startDays_, sy, sm, sd);
		
        if (sd <= dim && n < cap)
		{
			out[n++] = daysFromCivil(y, m, sd);
		}
    }
	
    sortDays(out, n);
    return n;
}

void IcalParser::applyBySetPos(int64_t* days, int32_t& n) const
{
    if (bySetPosCount_ == 0 || n == 0)
	{
		return;
	}
	
    int32_t pn = 0;

	for (int32_t i = 0; i < bySetPosCount_; ++i)
	{
        int pos = bySetPos_[i];
        int idx = pos > 0 ? pos - 1 : n + pos;
        if (idx >= 0 && idx < n && pn < ICAL_MAX_MDAYS)
		{
			picked_[pn++] = days[idx];
		}
    }

    sortDays(picked_, pn);
    for (int32_t i = 0; i < pn; ++i)
	{
		days[i] = picked_[i];
	}
    n = pn;
}

bool IcalParser::emitPeriod(int64_t* days, int32_t n, int32_t sod, int32_t& seq)
{
    bool bounded = (count_ > 0);
    for (int32_t i = 0; i < n; ++i)
	{
        int64_t day = days[i];
        if (day < startDays_) continue;
		
        int64_t utc = startIsUtc_ ? (day * 86400 + sod) : toUtc(day, sod);
        if (utc > untilUtc_)
		{
			return true;
		}
		
        ++seq;
        if (bounded && seq > count_)
		{
			return true;
		}
		
        emit(day, sod);
    }
    return false;
}
