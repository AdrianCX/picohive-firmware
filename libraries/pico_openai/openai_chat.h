/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>
#include "buffer_chain.h"
#include "pico_mjson.h"
#include "shared_buffer.h"


#define MAX_OPENAI_HOST 96
#define MAX_OPENAI_PATH 128

struct OpenAIEndpoint
{
    char host[MAX_OPENAI_HOST] = {};
    char path[MAX_OPENAI_PATH] = {};
    char conversations[MAX_OPENAI_PATH] = {};
    uint16_t port = 443;
    bool tls = true;
};

bool openai_parse_url(const char *url, OpenAIEndpoint& endpoint);


#define OPENAI_TOOL_DESCRIPTION_MAX 64

uint32_t openai_escape(const char *text, uint32_t len, char *out, uint32_t size);

uint32_t openai_tool(const char *name, const char *description, char *out, uint32_t size);

bool openai_append_escaped(BufferChain& out, const char *text, uint32_t len);


#ifndef MAX_OPENAI_ANSWER_BYTES
#define MAX_OPENAI_ANSWER_BYTES 8192
#endif

#ifndef OPENAI_AGENT_MAX_PARALLEL
#define OPENAI_AGENT_MAX_PARALLEL 8
#endif

#define OPENAI_CALL_ARGS_MAX 128

struct OpenAICall
{
    char id[64];
    char name[72];
    char args[OPENAI_CALL_ARGS_MAX];
};

enum class OpenAIPart : uint8_t
{
    Id,
    Error,
    Item,
};

class OpenAIResponseSink
{
public:
    virtual bool on_part(OpenAIPart part, const JsonView& value) = 0;
};

class OpenAIResponseReader : public OpenAIResponseSink
{
public:
    OpenAIResponseReader() = default;
    OpenAIResponseReader(OpenAIResponseReader&& other) noexcept;
    OpenAIResponseReader& operator=(OpenAIResponseReader&& other) noexcept;
    OpenAIResponseReader(const OpenAIResponseReader&) = delete;
    OpenAIResponseReader& operator=(const OpenAIResponseReader&) = delete;
    ~OpenAIResponseReader();

    void reset();

    bool on_part(OpenAIPart part, const JsonView& value) override;

    void end();

    bool usable() const;

    const char *id() const { return m_id; }
    const SharedBuffer& error() const { return m_error; }
    const SharedBuffer& text() const { return m_text; }

    uint32_t calls() const { return m_count; }
    const OpenAICall& call(uint32_t index) const { return m_calls[index]; }

    bool too_many_calls() const { return m_too_many; }

    OpenAICall *take_calls(uint32_t& count);

private:
    bool read_message(const JsonView& item);
    bool read_call(const JsonView& item);

    char m_id[96] = {};
    SharedBuffer m_error;
    SharedBuffer m_text;
    OpenAICall *m_calls = nullptr;
    uint32_t m_count = 0;
    bool m_too_many = false;
};

class OpenAIResponseFilter
{
public:
    void reset();
    void set_sink(OpenAIResponseSink *sink) { m_sink = sink; }

    bool feed(const char *in, uint32_t len);

    bool raw() const { return m_state == State::Raw; }

private:
    enum class State : uint8_t
    {
        Start,
        Raw,
        Value,
        Key,
        KeyString,
        Colon,
        String,
        Scalar,
        After,
    };

    enum class Top : uint8_t
    {
        Other,
        Id,
        Error,
        Output,
    };

    struct Level
    {
        bool object;
        bool keep;
        uint16_t count;
    };

    static const int MAX_DEPTH = 24;

    bool keep_key() const;
    void emit(const char *text, uint32_t len);
    void start_value(char c);
    void close_level();
    void value_done();

    Level m_level[MAX_DEPTH] = {};
    int m_depth = 0;
    State m_state = State::Start;
    bool m_escape = false;
    bool m_keep_value = false;
    bool m_in_object = false;
    char m_key[32] = {};
    uint32_t m_key_len = 0;
    Top m_top = Top::Other;
    bool m_in_output = false;

    bool m_capturing = false;
    OpenAIPart m_part = OpenAIPart::Item;
    int m_part_depth = 0;
    SharedBuffer m_buffer;
    bool m_stopped = false;
    OpenAIResponseSink *m_sink = nullptr;
};
