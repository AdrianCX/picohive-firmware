/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <utility>

#include "os_platform.h"

#include "app_config.h"
#include "memory_stats.h"
#include "openai_manager.h"
#include "pico_logger.h"

extern void openai_agent_load_config(const JsonView& config) __attribute__((weak));
extern void openai_agent_update(uint64_t now_us) __attribute__((weak));

const char *OpenAIManager::USER_AGENT = "RP2040 custom agent - github.com/AdrianCX/pico_https_example";

OpenAIManager& OpenAIManager::get()
{
    static OpenAIManager instance;
    return instance;
}

OpenAIManager::OpenAIManager()
    : m_client(m_endpoint.host, 443, true)
{
    m_client.setHttpCallback(this);
    m_client.setKeepAlive(OPENAI_KEEPALIVE_IDLE_S * 1000, OPENAI_KEEPALIVE_INTERVAL_S * 1000, OPENAI_KEEPALIVE_COUNT);
}

void OpenAIManager::load_config(const JsonView& config)
{
    if (openai_agent_load_config != NULL)
    {
        openai_agent_load_config(config);
    }

    if (config.same_string("$.openai.url", m_config.url) &&
        config.same_string("$.openai.key", m_config.api_key) &&
        config.same_string("$.openai.model", m_config.model))
    {
        trace("OpenAIManager::load_config: unchanged");
        return;
    }

    if (m_client.busy())
    {
        m_client.close();
    }

    config.get_string("$.openai.url", m_config.url, sizeof(m_config.url));
    config.get_string("$.openai.key", m_config.api_key, sizeof(m_config.api_key));
    config.get_string("$.openai.model", m_config.model, sizeof(m_config.model));

    apply_config();

    trace("OpenAIManager::load_config: url[%s] api_key[%s] model[%s] new config", m_config.url, (m_config.api_key[0] != 0) ? "*" : "", m_config.model);
}

void OpenAIManager::apply_config()
{
    m_configured = (m_config.url[0] != 0) && openai_parse_url(m_config.url, m_endpoint);

    if ((m_config.url[0] != 0) && !m_configured)
    {
        trace("openai: url [%s] is not usable, expected http(s)://host[:port][/path]", m_config.url);
    }

    if (!m_configured)
    {
        m_endpoint = OpenAIEndpoint();
    }

    m_client.setTarget(m_endpoint.host, m_endpoint.port, m_endpoint.tls);
}

bool OpenAIManager::ask(const char *system, const char *prompt, OpenAIAnswer done, void *context)
{
    if (!m_configured || (done == NULL) || (prompt == NULL))
    {
        return false;
    }

    if (busy())
    {
        trace("openai: a question is already out");
        return false;
    }

    system = (system != NULL) ? system : "";
    BufferChain body;
    body.append("{\"store\":false");

    if (m_config.model[0] != 0)
    {
        body.append(",\"model\":\"");
        openai_append_escaped(body, m_config.model, strlen(m_config.model));
        body.append("\"");
    }

    if (system[0] != 0)
    {
        body.append(",\"instructions\":\"");
        openai_append_escaped(body, system, strlen(system));
        body.append("\"");
    }

    body.append(",\"input\":\"");
    openai_append_escaped(body, prompt, strlen(prompt));
    body.append("\"}");

    if (body.full())
    {
        m_error = "no memory for the request";
        trace("openai: no memory for a %u byte request", (unsigned int)body.size());
        return false;
    }

    m_done = done;
    m_context = context;

    if (!start(m_endpoint.path, body))
    {
        m_done = NULL;
        m_context = NULL;
        return false;
    }

    return true;
}

bool OpenAIManager::post(const BufferChain& body, OpenAIResponseDone done, void *context, bool to_conversations)
{
    if (!m_configured || (done == NULL) || body.empty() || body.full())
    {
        return false;
    }

    if (busy())
    {
        trace("openai: a question is already out");
        return false;
    }

    m_raw_done = done;
    m_context = context;

    if (!start(to_conversations ? m_endpoint.conversations : m_endpoint.path, body))
    {
        m_raw_done = NULL;
        m_context = NULL;
        return false;
    }

    return true;
}

bool OpenAIManager::start(const char *path, const BufferChain& body)
{
    if (m_client.busy() || m_complete)
    {
        m_error = "a question is already out";
        trace("openai: a question is already out");
        return false;
    }

    uint32_t free_heap = getFreeHeap();
    uint32_t largest = getLargestFreeBlock();
    uint32_t needed = OPENAI_MIN_FREE_HEAP;

    if ((free_heap < needed) || (largest < OPENAI_MIN_FREE_BLOCK))
    {
        m_error = "not enough free memory on the board to connect to OpenAI";
        trace("openai: %u bytes free, largest block %u, needs %u and %u: not asking",
              (unsigned int)free_heap, (unsigned int)largest, (unsigned int)needed, (unsigned int)OPENAI_MIN_FREE_BLOCK);
        return false;
    }

    m_answer_failed = false;
    m_received = 0;
    m_reader.reset();
    m_filter.reset();
    m_filter.set_sink(&m_reader);
    m_complete = false;
    m_ok = false;
    m_code = 0;

    char authorization[MAX_OPENAI_KEY_SIZE + 16] = {};
    snprintf(authorization, sizeof(authorization), "Bearer %s", m_config.api_key);

    bool built = m_client.begin("POST", path) &&
                 m_client.addHeader("User-Agent", USER_AGENT) &&
                 m_client.addHeader("Content-Type", "application/json") &&
                 m_client.addHeader("Accept", "application/json") &&
                 m_client.addHeader("Connection", "close") &&
                 ((m_config.api_key[0] == 0) || m_client.addHeader("Authorization", authorization));

    memset(authorization, 0, sizeof(authorization));

    m_started_us = to_us_since_boot(get_absolute_time());
    m_next_status_us = m_started_us + (OPENAI_STATUS_S * 1000000ULL);
    m_last_rx_us = m_started_us;
    m_header_us = 0;

    trace("openai: asking %s:%d%s tls=%d model[%s] key[%s], %u byte request, timeout %ds",
          m_endpoint.host, m_endpoint.port, path, m_endpoint.tls, m_config.model,
          (m_config.api_key[0] != 0) ? "set" : "none", (unsigned int)body.size(), OPENAI_TIMEOUT_S);

    bool sent = built && m_client.send(body);

    if (!sent)
    {
        m_error = "the request failed to start";
        trace("openai: request failed to start");
        return false;
    }

    return true;
}

void OpenAIManager::update(uint64_t now_us)
{
    if (openai_agent_update != NULL)
    {
        openai_agent_update(now_us);
    }

    if (!busy())
    {
        return;
    }

    m_client.update();

    uint32_t elapsed_ms = (now_us > m_started_us) ? (uint32_t)((now_us - m_started_us) / 1000) : 0;
    uint32_t idle_ms = (now_us > m_last_rx_us) ? (uint32_t)((now_us - m_last_rx_us) / 1000) : 0;

    if (!m_complete)
    {
        if (now_us >= m_next_status_us)
        {
            m_next_status_us = now_us + (OPENAI_STATUS_S * 1000000ULL);

            trace("openai: still waiting after %us: %s, code %d, %u bytes so far, %us since bytes, timeout at %ds",
                  (unsigned int)(elapsed_ms / 1000), m_client.stateName(), m_code,
                  (unsigned int)m_received, (unsigned int)(idle_ms / 1000), OPENAI_TIMEOUT_S);
        }

        if (idle_ms > (OPENAI_TIMEOUT_S * 1000))
        {
            trace("openai: nothing received in %ds (%s, code %d, %u bytes after %ums), giving up",
                  OPENAI_TIMEOUT_S, m_client.stateName(), m_code, (unsigned int)m_received, (unsigned int)elapsed_ms);
            m_client.close();
        }

        if (!m_complete)
        {
            return;
        }
    }

    bool read_ok = m_ok && !m_answer_failed && (m_code == 200);
    OpenAIResponseReader answer = std::move(m_reader);
    answer.end();
    m_filter.reset();

    if (m_raw_done != NULL)
    {
        if (!read_ok)
        {
            trace("openai: no usable answer after %ums: complete=%d code=%d, %u bytes, read=%d",
                  (unsigned int)elapsed_ms, m_ok, m_code, (unsigned int)m_received, !m_answer_failed);

            if (!answer.error().empty())
            {
                trace("openai: error: %.*s", (int)((answer.error().size() < 1000) ? answer.error().size() : 1000), answer.error().c_str());
            }
        }

        OpenAIResponseDone raw_done = m_raw_done;
        void *raw_context = m_context;

        m_raw_done = NULL;
        m_context = NULL;
        m_complete = false;

        raw_done(read_ok, answer, raw_context);
        return;
    }

    bool ok = read_ok && answer.usable() && !answer.text().empty();

    if (m_ok && (m_code != 200))
    {
        answer.usable();
    }

    SharedBuffer text = answer.text();

    if (text.size() > MAX_OPENAI_ASK_TEXT)
    {
        text.set_size(MAX_OPENAI_ASK_TEXT);
    }

    if (ok)
    {
        trace("openai: answer after %ums (header after %ums), %u bytes: %.200s",
              (unsigned int)elapsed_ms,
              (m_header_us > m_started_us) ? (unsigned int)((m_header_us - m_started_us) / 1000) : 0,
              (unsigned int)m_received, text.c_str());
    }
    else
    {
        trace("openai: no usable answer after %ums: complete=%d code=%d, %u bytes, read=%d, caller falls back",
              (unsigned int)elapsed_ms, m_ok, m_code, (unsigned int)m_received, !m_answer_failed);
    }

    OpenAIAnswer done = m_done;
    void *context = m_context;

    m_done = NULL;
    m_context = NULL;
    m_complete = false;

    done(ok, ok ? text.c_str() : NULL, context);
}

bool OpenAIManager::onHeaderReceived(HTTPHeader& header)
{
    m_code = header.getResponseCode();
    m_header_us = to_us_since_boot(get_absolute_time());
    m_last_rx_us = m_header_us;

    trace("openai: answered %d after %ums", m_code, (unsigned int)((m_header_us - m_started_us) / 1000));
    return true;
}

bool OpenAIManager::onHttpData(u8_t *data, size_t len)
{
    m_last_rx_us = to_us_since_boot(get_absolute_time());

    bool read = m_filter.feed((const char *)data, len);

    if (m_filter.raw() && (m_received < 300))
    {
        uint32_t shown = ((300 - m_received) < len) ? (300 - m_received) : len;
        trace("openai: answer is not json: %.*s", (int)shown, (const char *)data);
    }

    m_received += len;

    if (!read)
    {
        m_answer_failed = true;
        return false;
    }

    return true;
}

void OpenAIManager::onResponseComplete(bool ok)
{
    trace("openai: request complete, ok=%d code=%d, %u bytes", ok, m_code, (unsigned int)m_received);

    m_ok = ok;
    m_complete = true;
}
