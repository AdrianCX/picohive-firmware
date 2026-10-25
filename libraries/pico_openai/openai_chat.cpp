/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include "openai_chat.h"
#include "pico_logger.h"
#include "pico_mjson.h"

static const char *CHAT_COMPLETIONS = "/chat/completions";
static const char *RESPONSES = "/responses";
static const char *CONVERSATIONS = "/conversations";

bool openai_parse_url(const char *url, OpenAIEndpoint& endpoint)
{
    endpoint = OpenAIEndpoint();

    if (url == NULL)
    {
        return false;
    }

    if (strncmp(url, "https://", 8) == 0)
    {
        url += 8;
        endpoint.tls = true;
        endpoint.port = 443;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        url += 7;
        endpoint.tls = false;
        endpoint.port = 80;
    }
    else
    {
        return false;
    }

    size_t host_len = strcspn(url, ":/");

    if ((host_len == 0) || (host_len >= MAX_OPENAI_HOST))
    {
        return false;
    }

    memcpy(endpoint.host, url, host_len);
    endpoint.host[host_len] = 0;
    url += host_len;

    if (*url == ':')
    {
        char *end = NULL;
        unsigned long port = strtoul(url + 1, &end, 10);

        if ((end == url + 1) || (port == 0) || (port > 65535) || ((*end != 0) && (*end != '/')))
        {
            return false;
        }

        endpoint.port = (uint16_t)port;
        url = end;
    }

    size_t path_len = strlen(url);

    while ((path_len > 0) && (url[path_len - 1] == '/'))
    {
        --path_len;
    }

    const char *methods[2] = { CHAT_COMPLETIONS, RESPONSES };

    for (const char *method : methods)
    {
        size_t method_len = strlen(method);

        if ((path_len >= method_len) && (strncmp(&url[path_len - method_len], method, method_len) == 0))
        {
            path_len -= method_len;
        }
    }

    if ((path_len + strlen(CONVERSATIONS)) >= MAX_OPENAI_PATH)
    {
        return false;
    }

    memcpy(endpoint.path, url, path_len);
    strcpy(&endpoint.path[path_len], RESPONSES);
    memcpy(endpoint.conversations, url, path_len);
    strcpy(&endpoint.conversations[path_len], CONVERSATIONS);

    return true;
}

uint32_t openai_escape(const char *text, uint32_t len, char *out, uint32_t size)
{
    static const char *HEX = "0123456789abcdef";
    uint32_t pos = 0;

    for (uint32_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)text[i];
        uint32_t need = ((c == '"') || (c == '\\') || (c == '\n') || (c == '\t') || (c == '\r')) ? 2 : ((c < 0x20) ? 6 : 1);

        if ((pos + need) > size)
        {
            return 0;
        }

        if (need == 1)
        {
            out[pos++] = (char)c;
        }
        else if (need == 2)
        {
            out[pos++] = '\\';
            out[pos++] = (c == '\n') ? 'n' : ((c == '\t') ? 't' : ((c == '\r') ? 'r' : (char)c));
        }
        else
        {
            memcpy(&out[pos], "\\u00", 4);
            out[pos + 4] = HEX[c >> 4];
            out[pos + 5] = HEX[c & 15];
            pos += 6;
        }
    }

    return pos;
}

uint32_t openai_tool(const char *name, const char *description, char *out, uint32_t size)
{
    int len = snprintf(out, size, "{\"type\":\"function\",\"name\":\"%s\",\"description\":\"", name);

    if ((len < 0) || ((uint32_t)len >= size))
    {
        return 0;
    }

    uint32_t text = strlen(description);
    uint32_t written = openai_escape(description, (text < OPENAI_TOOL_DESCRIPTION_MAX) ? text : OPENAI_TOOL_DESCRIPTION_MAX,
                                     &out[len], size - len);

    if ((written == 0) && (text > 0))
    {
        return 0;
    }

    int tail = snprintf(&out[len + written], size - len - written,
                        "\",\"parameters\":{\"type\":\"object\",\"properties\":{\"args\":{\"type\":\"string\"}}}}");

    if ((tail < 0) || ((uint32_t)tail >= (size - len - written)))
    {
        return 0;
    }

    return len + written + tail;
}

bool openai_append_escaped(BufferChain& out, const char *text, uint32_t len)
{
    char escaped[32 * 6];

    for (uint32_t done = 0; done < len; done += 32)
    {
        uint32_t piece = ((len - done) < 32) ? (len - done) : 32;
        uint32_t written = openai_escape(&text[done], piece, escaped, sizeof(escaped));

        if (!out.append(escaped, written))
        {
            return false;
        }
    }

    return true;
}


OpenAIResponseReader::OpenAIResponseReader(OpenAIResponseReader&& other) noexcept
{
    *this = std::move(other);
}

OpenAIResponseReader& OpenAIResponseReader::operator=(OpenAIResponseReader&& other) noexcept
{
    if (this != &other)
    {
        free(m_calls);

        memcpy(m_id, other.m_id, sizeof(m_id));
        m_error = std::move(other.m_error);
        m_text = std::move(other.m_text);
        m_calls = other.m_calls;
        m_count = other.m_count;
        m_too_many = other.m_too_many;

        other.m_id[0] = 0;
        other.m_calls = nullptr;
        other.m_count = 0;
        other.m_too_many = false;
    }

    return *this;
}

OpenAIResponseReader::~OpenAIResponseReader()
{
    free(m_calls);
}

void OpenAIResponseReader::reset()
{
    m_id[0] = 0;
    m_error = SharedBuffer();
    m_text = SharedBuffer();
    free(m_calls);
    m_calls = nullptr;
    m_count = 0;
    m_too_many = false;
}

bool OpenAIResponseReader::on_part(OpenAIPart part, const JsonView& value)
{
    switch (part)
    {
        case OpenAIPart::Id:
            if (!value.get_string("$", m_id, sizeof(m_id)))
            {
                trace("openai: the answer's id is not a string or is too long");
                m_id[0] = 0;
            }

            return true;

        case OpenAIPart::Error:
            if (value.type() != JsonType::Null)
            {
                m_error = SharedBuffer(value.length());
                m_error.append(value.data(), value.length());
            }

            return true;

        case OpenAIPart::Item:
            if (value.same_string("$.type", "message"))
            {
                return read_message(value);
            }

            if (value.same_string("$.type", "function_call"))
            {
                return read_call(value);
            }

            return true;
    }

    return true;
}

bool OpenAIResponseReader::read_message(const JsonView& item)
{
    if (item.type("$.content") != JsonType::Array)
    {
        return true;
    }

    for (const JsonEntry& part : item.find("$.content"))
    {
        if (!part.value.same_string("$.type", "output_text") || (part.value.type("$.text") != JsonType::String))
        {
            continue;
        }

        uint32_t room = part.value.find("$.text").length();
        uint32_t used = m_text.size();

        if ((used + room) > MAX_OPENAI_ANSWER_BYTES)
        {
            trace("openai: the answer's text is over %d bytes, dropping it", MAX_OPENAI_ANSWER_BYTES);
            return false;
        }

        if (!m_text.reserve(used + room))
        {
            trace("openai: no memory for %u bytes of the answer's text", (unsigned int)(used + room));
            return false;
        }

        part.value.get_string("$.text", m_text.data() + used, m_text.capacity() - used + 1);
        m_text.set_size(used + strlen(m_text.data() + used));
    }

    return true;
}

bool OpenAIResponseReader::read_call(const JsonView& item)
{
    if (!item.has("$.call_id") || !item.has("$.name"))
    {
        return true;
    }

    if (m_count >= OPENAI_AGENT_MAX_PARALLEL)
    {
        trace("openai: more than %d function calls at once, dropping the answer", OPENAI_AGENT_MAX_PARALLEL);
        m_too_many = true;
        return false;
    }

    OpenAICall *grown = (OpenAICall *)realloc(m_calls, (m_count + 1) * sizeof(OpenAICall));

    if (grown == nullptr)
    {
        trace("openai: no memory for function call %u", (unsigned int)(m_count + 1));
        return false;
    }

    m_calls = grown;
    OpenAICall& call = m_calls[m_count];
    memset(&call, 0, sizeof(call));

    if (!item.get_string("$.call_id", call.id, sizeof(call.id)) || !item.get_string("$.name", call.name, sizeof(call.name)))
    {
        trace("openai: function call %u has an id or name too long to read", (unsigned int)(m_count + 1));
        return false;
    }

    char arguments[(2 * OPENAI_CALL_ARGS_MAX) + 32];

    if (item.get_string("$.arguments", arguments, sizeof(arguments)))
    {
        JsonView parsed(arguments, strlen(arguments));

        if (!parsed.is_valid() || (parsed.type("$.args") != JsonType::String) ||
            !parsed.get_string("$.args", call.args, sizeof(call.args)))
        {
            call.args[0] = 0;
        }
    }

    ++m_count;
    return true;
}

void OpenAIResponseReader::end()
{
    char *text = m_text.data();

    if (text == nullptr)
    {
        return;
    }

    uint32_t used = m_text.size();
    uint32_t start = strspn(text, " \t\r\n");

    while ((used > start) && strchr(" \t\r\n", text[used - 1]))
    {
        --used;
    }

    memmove(text, &text[start], used - start);
    m_text.set_size(used - start);
}

bool OpenAIResponseReader::usable() const
{
    if (!m_error.empty())
    {
        JsonView error(m_error.c_str(), m_error.size());
        JsonView why = (error.type() == JsonType::String) ? error : error.find("$.message");
        trace("openai: refused: %.*s", (int)why.length(), why.data());
        return false;
    }

    if (m_id[0] == 0)
    {
        trace("openai: answer has no id");
        return false;
    }

    if (m_text.empty() && (m_count == 0))
    {
        trace("openai: answer has neither text nor a function call");
        return false;
    }

    return true;
}

OpenAICall *OpenAIResponseReader::take_calls(uint32_t& count)
{
    OpenAICall *calls = m_calls;
    count = m_count;

    m_calls = nullptr;
    m_count = 0;
    return calls;
}


void OpenAIResponseFilter::reset()
{
    m_depth = 0;
    m_state = State::Start;
    m_escape = false;
    m_key_len = 0;
    m_top = Top::Other;
    m_in_output = false;
    m_capturing = false;
    m_stopped = false;
    m_buffer = SharedBuffer();
}

bool OpenAIResponseFilter::keep_key() const
{
    static const char *TOP[] = { "id", "error", "output" };
    static const char *NEVER[] = { "encrypted_content", "summary", "annotations", "logprobs" };

    if (m_depth == 1)
    {
        for (const char *key : TOP)
        {
            if ((strlen(key) == m_key_len) && !memcmp(key, m_key, m_key_len))
            {
                return true;
            }
        }

        return false;
    }

    for (const char *key : NEVER)
    {
        if ((strlen(key) == m_key_len) && !memcmp(key, m_key, m_key_len))
        {
            return false;
        }
    }

    return true;
}

void OpenAIResponseFilter::emit(const char *text, uint32_t len)
{
    if (!m_capturing || m_stopped)
    {
        return;
    }

    uint32_t needed = m_buffer.size() + len;

    if (needed > MAX_OPENAI_ANSWER_BYTES)
    {
        trace("openai: a part of the answer is over %d bytes, dropping the answer", MAX_OPENAI_ANSWER_BYTES);
        m_stopped = true;
        return;
    }

    if (needed > m_buffer.capacity())
    {
        uint32_t grown = ((needed + 255) / 256) * 256;
        grown = (grown < MAX_OPENAI_ANSWER_BYTES) ? grown : MAX_OPENAI_ANSWER_BYTES;

        if (!m_buffer.reserve(grown))
        {
            trace("openai: no memory for %u bytes of a part of the answer", (unsigned int)grown);
            m_stopped = true;
            return;
        }
    }

    m_buffer.append(text, len);
}

void OpenAIResponseFilter::start_value(char c)
{
    if (!m_in_object && (m_depth > 0))
    {
        Level& parent = m_level[m_depth - 1];
        m_keep_value = parent.keep;

        if (m_keep_value && (parent.count++ > 0))
        {
            emit(",", 1);
        }
    }

    bool top_part = (m_depth == 1) && m_in_object && ((m_top == Top::Id) || (m_top == Top::Error));
    bool item = (m_depth == 2) && !m_in_object && m_in_output;

    if (m_keep_value && !m_capturing && (top_part || item))
    {
        m_capturing = true;
        m_part = item ? OpenAIPart::Item : ((m_top == Top::Id) ? OpenAIPart::Id : OpenAIPart::Error);
        m_part_depth = m_depth;
        m_buffer.clear();
    }

    if (m_keep_value)
    {
        emit(&c, 1);
    }

    if ((c == '{') || (c == '['))
    {
        if (m_depth >= MAX_DEPTH)
        {
            m_state = State::Raw;
            m_capturing = false;
            return;
        }

        if ((m_depth == 1) && m_in_object && (m_top == Top::Output) && (c == '['))
        {
            m_in_output = true;
        }

        m_level[m_depth++] = { c == '{', m_keep_value, 0 };
        m_in_object = false;
        m_state = (c == '{') ? State::Key : State::Value;
        return;
    }

    m_escape = false;
    m_state = (c == '"') ? State::String : State::Scalar;
}

void OpenAIResponseFilter::close_level()
{
    --m_depth;

    if (m_depth == 1)
    {
        m_in_output = false;
    }

    value_done();
}

void OpenAIResponseFilter::value_done()
{
    if (!m_capturing || (m_depth != m_part_depth))
    {
        return;
    }

    m_capturing = false;

    if (!m_stopped && (m_sink != nullptr) && !m_sink->on_part(m_part, JsonView(m_buffer.c_str(), m_buffer.size())))
    {
        m_stopped = true;
    }

    m_buffer.clear();
}

bool OpenAIResponseFilter::feed(const char *in, uint32_t len)
{
    for (uint32_t i = 0; (i < len) && !m_stopped; ++i)
    {
        char c = in[i];
        bool space = (c == ' ') || (c == '\n') || (c == '\r') || (c == '\t');

        switch (m_state)
        {
            case State::Start:
                if (space)
                {
                    break;
                }

                if (c != '{')
                {
                    m_state = State::Raw;
                    break;
                }

                m_in_object = true;
                m_keep_value = true;
                start_value(c);
                break;

            case State::Raw:
                break;

            case State::Value:
                if (space)
                {
                    break;
                }

                if ((c == ']') && (m_depth > 0) && !m_level[m_depth - 1].object)
                {
                    if (m_level[m_depth - 1].keep)
                    {
                        emit(&c, 1);
                    }

                    m_state = State::After;
                    close_level();
                    break;
                }

                start_value(c);
                break;

            case State::Key:
                if (space || (c == ','))
                {
                    break;
                }

                if (c == '}')
                {
                    if (m_level[m_depth - 1].keep)
                    {
                        emit(&c, 1);
                    }

                    m_state = State::After;
                    close_level();
                    break;
                }

                if (c == '"')
                {
                    m_key_len = 0;
                    m_escape = false;
                    m_state = State::KeyString;
                }

                break;

            case State::KeyString:
                if (!m_escape && (c == '"'))
                {
                    m_state = State::Colon;
                    break;
                }

                m_escape = !m_escape && (c == '\\');

                if (m_key_len < sizeof(m_key))
                {
                    m_key[m_key_len] = c;
                }

                ++m_key_len;
                break;

            case State::Colon:
                if (c != ':')
                {
                    break;
                }

                {
                    Level& parent = m_level[m_depth - 1];
                    m_key_len = (m_key_len <= sizeof(m_key)) ? m_key_len : (uint32_t)sizeof(m_key) + 1;
                    m_keep_value = parent.keep && (m_key_len <= sizeof(m_key)) && keep_key();

                    if (m_depth == 1)
                    {
                        m_top = !m_keep_value ? Top::Other
                              : ((m_key_len == 2) && !memcmp(m_key, "id", 2)) ? Top::Id
                              : ((m_key_len == 5) && !memcmp(m_key, "error", 5)) ? Top::Error
                              : Top::Output;
                    }

                    if (m_keep_value)
                    {
                        if (parent.count++ > 0)
                        {
                            emit(",", 1);
                        }

                        emit("\"", 1);
                        emit(m_key, m_key_len);
                        emit("\":", 2);
                    }
                }

                m_in_object = true;
                m_state = State::Value;
                break;

            case State::String:
                if (m_keep_value)
                {
                    emit(&c, 1);
                }

                if (!m_escape && (c == '"'))
                {
                    m_state = State::After;
                    value_done();
                    break;
                }

                m_escape = !m_escape && (c == '\\');
                break;

            case State::Scalar:
                if (!space && (c != ',') && (c != '}') && (c != ']'))
                {
                    if (m_keep_value)
                    {
                        emit(&c, 1);
                    }

                    break;
                }

                m_state = State::After;
                value_done();
                --i;
                break;

            case State::After:
                if (space || (m_depth == 0))
                {
                    break;
                }

                if (c == ',')
                {
                    m_in_object = m_level[m_depth - 1].object;
                    m_state = m_in_object ? State::Key : State::Value;
                    break;
                }

                if ((c == '}') || (c == ']'))
                {
                    if (m_level[m_depth - 1].keep)
                    {
                        emit(&c, 1);
                    }

                    close_level();
                }

                break;
        }
    }

    return !m_stopped;
}
