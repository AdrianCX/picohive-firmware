/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "calendar_util.h"
#include "calendar_schedule.h"
#include "command_dispatcher.h"
#include "command_registration.h"
#include "http_client.h"
#include "http_request.h"
#include "ical_parser.h"
#include "mbedtls/build_info.h"
#include "pico_mjson.h"

#define MAX_CALENDAR_URL 256
#define MAX_CALENDAR_ID 32
#define MAX_CALENDAR_ALLOWED 256

#ifndef CALENDAR_FETCH_INTERVAL_S
#define CALENDAR_FETCH_INTERVAL_S 3600
#endif

#ifndef CALENDAR_RETRY_S
#define CALENDAR_RETRY_S 60
#endif

// Left over once a fetch has its CalendarFetch: the TLS session's buffers and handshake.
#ifndef CALENDAR_MIN_FREE_HEAP
#define CALENDAR_MIN_FREE_HEAP (MBEDTLS_SSL_IN_CONTENT_LEN + MBEDTLS_SSL_OUT_CONTENT_LEN + 12288)
#endif

#ifndef CALENDAR_LOOKAHEAD_S
#define CALENDAR_LOOKAHEAD_S (7 * 86400)
#endif

struct CalendarCommandsConfig
{
    char url[MAX_CALENDAR_URL] = {};           // the iCal address, a secret
    char id[MAX_CALENDAR_ID] = {};             // what titles start with: "<id>/water/1/10"
    char allowed[MAX_CALENDAR_ALLOWED] = {};   // what they may run: "water stop"
    uint32_t timezone = TZ_CENTRAL_EUROPE;     // Timezone, for events written in local time
};

class CalendarCommandContext : public CommandContext
{
public:
    uint32_t send_space() override;

protected:
    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;
};

struct CalendarFetch
{
    CalendarFetch(IHttpCallback *callback);

    char host[MAX_CALENDAR_URL] = {};
    HTTPClient client;
    IcalParser parser;
};

class CalendarCommands : public IHttpCallback
{
public:
    static CalendarCommands& get();

    void load_config(const JsonView& config);

    const CalendarCommandsConfig& get_config() const { return m_config; }

    bool is_configured() const { return (m_config.url[0] != 0) && (m_config.id[0] != 0); }

    void update(uint64_t now_us);

    bool refresh();

    bool clock_known() const { return m_clock_known; }
    bool is_fetching() const { return m_fetching; }
    uint64_t next_fetch_us() const { return m_next_fetch_us; }
    int64_t now_utc() const;
    int32_t tz_offset() const { return m_tz_offset; }
    const CalendarSchedule& schedule() const { return m_schedule; }
    bool last_fetch_ok() const { return m_last_ok; }
    uint64_t last_fetch_us() const { return m_last_fetch_us; }
    const char *last_error() const { return m_error; }
    uint64_t now_us() const { return m_now_us; }

protected:
    bool onHeaderReceived(HTTPHeader& header) override;
    bool onHttpData(u8_t *data, size_t len) override;
    void onResponseComplete(bool ok) override;

private:
    CalendarCommands();

    bool start_fetch();
    void drop_fetch();
    void fetch_failed(const char *error);
    void collect();
    void run_due();
    void run(std::string_view command, int64_t start_utc);

    CalendarCommandsConfig m_config;

    CalendarFetch *m_fetch = NULL;
    CalendarSchedule m_schedule;
    CalendarCommandContext m_context;
    CommandSlot m_slots[1];
    CommandDispatcher m_dispatcher;

    uint64_t m_now_us = 0;
    uint64_t m_next_fetch_us = 0;
    uint64_t m_fetch_started_us = 0;
    bool m_fetching = false;
    bool m_parsing = false;
    bool m_close_pending = false;
    bool m_clock_known = false;
    int64_t m_clock_utc = 0;
    uint64_t m_clock_us = 0;
    int32_t m_tz_offset = 0;
    bool m_last_ok = false;
    uint64_t m_last_fetch_us = 0;
    char m_error[64] = "not fetched yet";

    static const uint64_t STALL_US = 60 * 1000000ULL;
};
