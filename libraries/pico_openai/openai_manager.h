/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "http_client.h"
#include "mbedtls/build_info.h"
#include "http_request.h"
#include "openai_chat.h"
#include "pico_mjson.h"

#define MAX_OPENAI_URL_SIZE 128
#define MAX_OPENAI_KEY_SIZE 256
#define MAX_OPENAI_MODEL_SIZE 64

#ifndef OPENAI_TIMEOUT_S
#define OPENAI_TIMEOUT_S 120
#endif

#ifndef OPENAI_KEEPALIVE_IDLE_S
#define OPENAI_KEEPALIVE_IDLE_S 15
#endif

#ifndef OPENAI_KEEPALIVE_INTERVAL_S
#define OPENAI_KEEPALIVE_INTERVAL_S 5
#endif

#ifndef OPENAI_KEEPALIVE_COUNT
#define OPENAI_KEEPALIVE_COUNT 4
#endif

#ifndef OPENAI_STATUS_S
#define OPENAI_STATUS_S 5
#endif

#ifndef OPENAI_ANSWER_EXPECTED
#define OPENAI_ANSWER_EXPECTED 3072
#endif

#ifndef OPENAI_MIN_FREE_HEAP
#define OPENAI_MIN_FREE_HEAP (MBEDTLS_SSL_IN_CONTENT_LEN + MBEDTLS_SSL_OUT_CONTENT_LEN + OPENAI_ANSWER_EXPECTED + 12288)
#endif

#ifndef OPENAI_MIN_FREE_BLOCK
#define OPENAI_MIN_FREE_BLOCK (MBEDTLS_SSL_IN_CONTENT_LEN + 1024)
#endif

struct OpenAIConfig
{
    char url[MAX_OPENAI_URL_SIZE] = {};
    char api_key[MAX_OPENAI_KEY_SIZE] = {};
    char model[MAX_OPENAI_MODEL_SIZE] = {};
};

typedef void (*OpenAIAnswer)(bool ok, const char *text, void *context);

#ifndef MAX_OPENAI_ASK_TEXT
#define MAX_OPENAI_ASK_TEXT 1024
#endif
typedef void (*OpenAIResponseDone)(bool ok, OpenAIResponseReader& answer, void *context);

class OpenAIManager : public IHttpCallback
{
public:
    static OpenAIManager& get();

    void load_config(const JsonView& config);
    const OpenAIConfig& get_config() { return m_config; }

    bool is_configured() const { return m_configured; }

    bool busy() const { return (m_done != NULL) || (m_raw_done != NULL); }

    bool ask(const char *system, const char *prompt, OpenAIAnswer done, void *context = NULL);

    bool post(const BufferChain& body, OpenAIResponseDone done, void *context, bool to_conversations = false);

    const char *get_url() const { return m_config.url; }

    const char *last_error() const { return m_error; }

    void update(uint64_t now_us);

private:
    OpenAIManager();

    bool onHeaderReceived(HTTPHeader& header) override;
    bool onHttpData(u8_t *data, size_t len) override;
    void onResponseComplete(bool ok) override;

    void apply_config();
    bool start(const char *path, const BufferChain& body);

    OpenAIConfig m_config;
    OpenAIEndpoint m_endpoint;
    bool m_configured = false;

    OpenAIAnswer m_done = NULL;
    const char *m_error = "";
    OpenAIResponseDone m_raw_done = NULL;
    void *m_context = NULL;
    uint64_t m_started_us = 0;
    uint64_t m_next_status_us = 0;
    uint64_t m_header_us = 0;
    uint64_t m_last_rx_us = 0;

    bool m_complete = false;
    bool m_ok = false;
    uint16_t m_code = 0;

    OpenAIResponseFilter m_filter;
    OpenAIResponseReader m_reader;
    bool m_answer_failed = false;
    uint32_t m_received = 0;

    static const char *USER_AGENT;

    HTTPClient m_client;
};
