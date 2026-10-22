/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app_config.h"
#include "calendar_commands.h"
#include "http_header_helper.h"
#include "memory_stats.h"
#include "pico_logger.h"
#include "upgrade_helper.h"


bool CalendarCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    trace("calendar: %.*s answered: %.*s", (int)name.size(), name.data(), (int)len, (const char *)data);
    return true;
}

bool CalendarCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    trace("calendar: %.*s cannot stream to a calendar", (int)name.size(), name.data());
    return false;
}

uint32_t CalendarCommandContext::send_space()
{
    return UINT32_MAX;
}

bool CalendarCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    return false;
}

CalendarCommands& CalendarCommands::get()
{
    static CalendarCommands instance;
    return instance;
}

CalendarFetch::CalendarFetch(IHttpCallback *callback)
    : client(host, 443, true)
{
    client.addWantedHeader("date");
    client.setHttpCallback(callback);
}

CalendarCommands::CalendarCommands()
    : m_dispatcher(CalendarCommandSet::all(), m_context, m_slots)
{
}

void CalendarCommands::load_config(const JsonView& config)
{
    uint32_t timezone = TZ_CENTRAL_EUROPE;
    if (!config.get_uint("$.calendar.timezone", timezone) || (timezone >= (uint32_t)TZ_COUNT))
    {
        timezone = TZ_CENTRAL_EUROPE;
    }

    bool source_changed = !config.same_string("$.calendar.secret_url", m_config.url) ||
                          !config.same_string("$.calendar.id", m_config.id) ||
                          (timezone != m_config.timezone);

    if (!source_changed && config.same_string("$.calendar.allowed", m_config.allowed))
    {
        return;
    }

    config.get_string("$.calendar.secret_url", m_config.url, sizeof(m_config.url));
    config.get_string("$.calendar.id", m_config.id, sizeof(m_config.id));
    config.get_string("$.calendar.allowed", m_config.allowed, sizeof(m_config.allowed));
    m_config.timezone = timezone;

    trace("calendar: new config, id[%s] allowed[%s] timezone[%u] url[%s]", m_config.id, m_config.allowed, (unsigned int)m_config.timezone, (m_config.url[0] != 0) ? "*" : "");

    if (source_changed)
    {
        if (m_fetching)
        {
            m_fetch->client.close();
        }

        m_schedule.reset();
        m_clock_known = false;
    }

    m_next_fetch_us = 0;
}

int64_t CalendarCommands::now_utc() const
{
    return m_clock_utc + (int64_t)((m_now_us - m_clock_us) / 1000000ULL);
}

void CalendarCommands::update(uint64_t now_us)
{
    m_now_us = now_us;

    if (m_fetch != NULL)
    {
        m_fetch->client.update();
    }

    if (m_fetching)
    {
        if ((now_us - m_fetch_started_us) > STALL_US)
        {
            trace("calendar: fetch open for %llus, dropping the connection", (unsigned long long)((now_us - m_fetch_started_us) / 1000000ULL));
            m_fetch->client.close();
        }
    }
    else if (m_close_pending)
    {
        m_close_pending = false;
        drop_fetch();
    }

    if (!is_configured())
    {
        return;
    }

    if (!m_fetching && (m_fetch == NULL) && (now_us >= m_next_fetch_us) && !UpgradeHelper::isUpgrading())
    {
        start_fetch();
    }

    run_due();
}

bool CalendarCommands::refresh()
{
    if (!is_configured())
    {
        return false;
    }

    if (!m_fetching)
    {
        m_next_fetch_us = 0;
    }

    trace("calendar: refresh asked for%s", m_fetching ? ", already fetching" : "");
    return true;
}

bool CalendarCommands::start_fetch()
{
    uint32_t free_heap = getFreeHeap();
    uint32_t largest = getLargestFreeBlock();

    if ((free_heap < (sizeof(CalendarFetch) + CALENDAR_MIN_FREE_HEAP)) || (largest < sizeof(CalendarFetch)))
    {
        trace("calendar: %u bytes free, largest block %u, a fetch needs %u and %u more for TLS",
              (unsigned int)free_heap, (unsigned int)largest, (unsigned int)sizeof(CalendarFetch), (unsigned int)CALENDAR_MIN_FREE_HEAP);
        fetch_failed("not enough free memory");
        return false;
    }

    m_fetch = new CalendarFetch(this);

    // Freed in update(), once the client is done with it.
    m_close_pending = true;

    char path[MAX_CALENDAR_URL] = {};
    const char *url = (strncasecmp(m_config.url, "webcal://", 9) == 0) ? (m_config.url + 9) : m_config.url;

    if (!HTTPHeaderHelper::getHost(url, m_fetch->host, sizeof(m_fetch->host)) || !HTTPHeaderHelper::getPath(url, path, sizeof(path)))
    {
        fetch_failed("bad calendar address");
        return false;
    }

    m_parsing = false;
    m_error[0] = 0;

    if (!m_fetch->client.begin("GET", path) || !m_fetch->client.send())
    {
        fetch_failed("request not sent");
        return false;
    }

    m_fetching = true;
    m_fetch_started_us = m_now_us;
    return true;
}

// lwip still holds a session waiting on dns, so that one is kept for a later pass.
void CalendarCommands::drop_fetch()
{
    if (m_fetch == NULL)
    {
        return;
    }

    m_fetch->client.close();

    if (m_fetch->client.resolving())
    {
        m_close_pending = true;
        return;
    }

    delete m_fetch;
    m_fetch = NULL;
}

void CalendarCommands::fetch_failed(const char *error)
{
    trace("calendar: fetch failed: %s, next try in %us", error, (unsigned int)CALENDAR_RETRY_S);

    snprintf(m_error, sizeof(m_error), "%s", error);
    m_last_ok = false;
    m_next_fetch_us = m_now_us + (CALENDAR_RETRY_S * 1000000ULL);
}

bool CalendarCommands::onHeaderReceived(HTTPHeader& header)
{
    if (header.getResponseCode() != 200)
    {
        snprintf(m_error, sizeof(m_error), "answered %d", header.getResponseCode());
        return false;
    }

    const char *date = header.getHeaderValue("date");
    if (date == NULL)
    {
        snprintf(m_error, sizeof(m_error), "no Date in the answer");
        return false;
    }

    int64_t utc = httpDateToUtc(date);
    if (utc == INT64_MIN)
    {
        snprintf(m_error, sizeof(m_error), "Date not understood");
        return false;
    }

    m_clock_utc = utc;
    m_clock_us = m_now_us;
    m_clock_known = true;
    m_tz_offset = timezoneOffset((Timezone)m_config.timezone, utc);

    m_schedule.clock_known(utc);

    m_fetch->parser.begin(utc - CALENDAR_LATE_S, utc + CALENDAR_LOOKAHEAD_S, m_tz_offset);
    m_parsing = true;
    return true;
}

bool CalendarCommands::onHttpData(u8_t *data, size_t len)
{
    if (m_parsing)
    {
        m_fetch->parser.feed((const char *)data, len);
    }

    return true;
}

void CalendarCommands::onResponseComplete(bool ok)
{
    m_fetching = false;
    m_close_pending = true;

    if (!ok || !m_parsing)
    {
        char error[sizeof(m_error)];
        snprintf(error, sizeof(error), "%s", m_parsing ? "answer cut short" : ((m_error[0] != 0) ? m_error : "no answer"));

        m_parsing = false;
        fetch_failed(error);
        return;
    }

    m_parsing = false;
    m_fetch->parser.end();

    collect();

    m_last_ok = true;
    m_last_fetch_us = m_now_us;
    m_error[0] = 0;
    m_next_fetch_us = m_now_us + (CALENDAR_FETCH_INTERVAL_S * 1000000ULL);
}

void CalendarCommands::collect()
{
    m_schedule.clear();

    uint32_t dropped = 0;

    const IcalParser& parser = m_fetch->parser;

    for (int32_t i = 0; i < parser.count(); ++i)
    {
        const IcalEvent& event = parser.at(i);
        std::string_view command;

        if (!calendar_route(event.title, m_config.id, command))
        {
            continue;
        }

        if (event.allDay)
        {
            trace("calendar: [%s] is an all day event, it needs a start time to run", event.title);
            continue;
        }

        if (!m_schedule.add(event.startUtc, command))
        {
            ++dropped;
        }
    }

    trace("calendar: %d events in the next %u days, %u for [%s], %u left for later",
          (int)parser.count(), (unsigned int)(CALENDAR_LOOKAHEAD_S / 86400), (unsigned int)m_schedule.count(), m_config.id, (unsigned int)dropped);
}

void CalendarCommands::run_due()
{
    if (!m_clock_known)
    {
        return;
    }

    m_schedule.fire_due(now_utc(), [this](std::string_view command, int64_t start_utc) { run(command, start_utc); });
}

void CalendarCommands::run(std::string_view command, int64_t start_utc)
{
    int64_t late = now_utc() - start_utc;

    if (!calendar_allowed(m_config.allowed, command))
    {
        trace("calendar: %.*s is not on the allowed list [%s]", (int)command.size(), command.data(), m_config.allowed);
        return;
    }

    std::string_view args;
    const Command *found = CalendarCommandSet::find(command, args);

    if (found == nullptr)
    {
        trace("calendar: no calendar command for %.*s", (int)command.size(), command.data());
        return;
    }

    if (found->payload != Payload::None)
    {
        trace("calendar: %.*s takes a body, an event title has none", (int)found->name.size(), found->name.data());
        return;
    }

    trace("calendar: running %.*s, %llds after it started", (int)command.size(), command.data(), (long long)late);
    m_dispatcher.dispatch_header(command, 0);
}
