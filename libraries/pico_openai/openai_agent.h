/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "capture_command_context.h"
#include "command_registration.h"
#include "openai_chat.h"
#include "openai_manager.h"
#include "pico_mjson.h"
#include "shared_buffer.h"

#define MAX_OPENAI_INSTRUCTIONS 512

#ifndef OPENAI_AGENT_MAX_ROUNDS
#define OPENAI_AGENT_MAX_ROUNDS 25
#endif

#ifndef OPENAI_AGENT_MAX_CALLS
#define OPENAI_AGENT_MAX_CALLS 24
#endif

#ifndef OPENAI_AGENT_REPLY_BYTES
#define OPENAI_AGENT_REPLY_BYTES 768
#endif

#ifndef OPENAI_AGENT_CONTEXT_BYTES
#define OPENAI_AGENT_CONTEXT_BYTES 3072
#endif

#ifndef OPENAI_AGENT_MAX_REQUEST
#define OPENAI_AGENT_MAX_REQUEST 16384
#endif


#ifndef OPENAI_AGENT_TIMEOUT_S
#define OPENAI_AGENT_TIMEOUT_S 300
#endif

typedef bool (*OpenAIAgentDone)(const char *text, void *context);

class OpenAIAgent;

class AgentCommandContext : public CaptureCommandContext
{
public:
    OpenAIAgent *agent = nullptr;

protected:
    void take(const char *text, uint32_t len) override;
};

#define OPENAI_CONVERSATION_FILE "openai_conversation.bin"

class OpenAIAgent
{
public:
    static OpenAIAgent& get();

    void load_config(const JsonView& config);

    bool start(const SharedBuffer& prompt, OpenAIAgentDone done, void *context, const char *&why);
    void stop(const char *why);

    void update(uint64_t now_us);

    bool running() const { return m_state != State::Idle; }

    uint32_t status(char *out, uint32_t size) const;

    uint32_t answer_size() const { return m_answer.size(); }

    bool forget();

    bool append(const char *text, uint32_t len);
    bool append_escaped(const char *text, uint32_t len);

private:
    enum class State : uint8_t
    {
        Idle,
        Creating,
        Asking,
        Calling,
        Delivering,
    };

    OpenAIAgent();

    bool ask();
    bool create();
    void first_request();
    void begin_request();
    void run_call();
    void finish(const char *text);
    bool room_for(uint32_t len);
    void set_answer(const char *format, ...) __attribute__((format(printf, 2, 3)));

    static void on_answer(bool ok, OpenAIResponseReader& answer, void *context);
    static void on_created(bool ok, OpenAIResponseReader& answer, void *context);
    void created(bool ok, OpenAIResponseReader& answer);
    void answered(bool ok, OpenAIResponseReader& answer);

    SharedBuffer m_instructions;
    bool m_tools_enabled = true;
    const char *m_why = "";

    State m_state = State::Idle;

    BufferChain m_body;
    bool m_full = false;
    bool m_closed = false;

    char m_response_id[96] = {};

    struct Conversation
    {
        char url[MAX_OPENAI_URL_SIZE];
        char model[MAX_OPENAI_MODEL_SIZE];
        char id[96];
    };

    Conversation m_conversation = {};
    bool m_retried = false;
    uint32_t m_input_mark = 0;
    uint32_t m_lost_calls = 0;
    SharedBuffer m_prompt;
    OpenAICall *m_call = NULL;
    uint32_t m_calls = 0;
    uint32_t m_next_call = 0;

    uint32_t m_rounds = 0;
    uint32_t m_calls_run = 0;
    uint64_t m_started_us = 0;
    uint64_t m_delivering_us = 0;
    uint64_t m_now_us = 0;

    OpenAIAgentDone m_done = NULL;
    void *m_context = NULL;

    SharedBuffer m_answer;
    char m_topic[MAX_COMMAND_ARGS + 72] = {};

    AgentCommandContext m_command;
};
