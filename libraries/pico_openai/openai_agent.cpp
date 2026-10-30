/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_log.h"
#include "ai_tools.h"
#include "app_config.h"
#include "memory_stats.h"
#include "openai_agent.h"
#include "openai_manager.h"
#include "openai_sequence.h"
#include "pico_logger.h"

static const char *BASE_INSTRUCTIONS =
    "You control a small device. Plan the whole task at once and reply with one JSON object and nothing else: "
    "{\"steps\":[{\"do\":\"<command>\",\"ms\":<extra pause after it, usually 0>}, ...]}. "
    "The board runs the steps in order by itself, each once the one before has finished. "
    "Commands are the tools' names with their args. Use as few steps as the task needs. "
    "Call a tool only to read something the state below does not already say. "
    "If it cannot be done: {\"steps\":[],\"why\":\"<at most 8 words>\"}.";

static const uint64_t DELIVER_TIMEOUT_US = 120000000ULL;

void AgentCommandContext::take(const char *text, uint32_t len)
{
    agent->append_escaped(text, len);
}

OpenAIAgent& OpenAIAgent::get()
{
    static OpenAIAgent instance;
    return instance;
}

OpenAIAgent::OpenAIAgent()
{
    m_command.agent = this;
}

void OpenAIAgent::load_config(const JsonView& config)
{
    uint32_t room = (config.type("$.openai.instructions") == JsonType::String) ? config.find("$.openai.instructions").length() : 0;
    room = (room < MAX_OPENAI_INSTRUCTIONS) ? room : MAX_OPENAI_INSTRUCTIONS - 1;
    m_instructions = (room > 0) ? SharedBuffer(room) : SharedBuffer();

    if (m_instructions)
    {
        config.get_string("$.openai.instructions", m_instructions.data(), m_instructions.capacity() + 1);
        m_instructions.set_size(strlen(m_instructions.c_str()));
    }

    bool tools = true;
    m_tools_enabled = !config.get_bool("$.openai.tools", tools) || tools;

    trace("openai agent: %u tools, %u context commands, %s, %u bytes of instructions",
          (unsigned int)AiCommandSet::size(), (unsigned int)AiContextCommandSet::size(),
          m_tools_enabled ? "enabled" : "disabled", (unsigned int)m_instructions.size());
}

bool OpenAIAgent::room_for(uint32_t len)
{
    uint32_t size = m_body.size();

    if (len > (OPENAI_AGENT_MAX_REQUEST - size))
    {
        return false;
    }

    if (((size / BUFFER_CHAIN_BLOCK) != ((size + len) / BUFFER_CHAIN_BLOCK)) &&
        (getFreeHeap() < (OPENAI_MIN_FREE_HEAP + len + BUFFER_CHAIN_BLOCK)))
    {
        trace("openai agent: %u bytes free, not growing the conversation past %u", (unsigned int)getFreeHeap(), (unsigned int)size);
        return false;
    }

    return true;
}

bool OpenAIAgent::append(const char *text, uint32_t len)
{
    if (m_full || !room_for(len) || !m_body.append(text, len))
    {
        m_full = true;
        return false;
    }

    return true;
}

bool OpenAIAgent::append_escaped(const char *text, uint32_t len)
{
    char escaped[32 * 6];

    for (uint32_t done = 0; done < len; done += 32)
    {
        uint32_t piece = ((len - done) < 32) ? (len - done) : 32;

        if (!append(escaped, openai_escape(&text[done], piece, escaped, sizeof(escaped))))
        {
            return false;
        }
    }

    return true;
}

void OpenAIAgent::set_answer(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    va_list again;
    va_copy(again, ap);
    int len = vsnprintf(NULL, 0, format, ap);
    va_end(ap);

    m_answer = SharedBuffer((len > 0) ? (uint32_t)len : 0);
    m_answer.append_vsnprintf(format, again);
    va_end(again);
}

void OpenAIAgent::begin_request()
{
    const char *model = OpenAIManager::get().get_config().model;

    m_body = BufferChain(OPENAI_AGENT_MAX_REQUEST);
    m_full = false;
    m_closed = false;

    append("{\"store\":true", 13);

    if (model[0] != 0)
    {
        append(",\"model\":\"", 10);
        append_escaped(model, strlen(model));
        append("\"", 1);
    }

    append(",\"truncation\":\"auto\",\"conversation\":\"", 37);
    append_escaped(m_conversation.id, strlen(m_conversation.id));
    append("\"", 1);

    if (m_tools_enabled && (AiCommandSet::size() > 0))
    {
        append(",\"tools\":[", 10);
        bool first = true;

        char tool[256];
        char name[65];
        char help[OPENAI_TOOL_DESCRIPTION_MAX + 1];

        for (const Command& command : AiCommandSet::all())
        {
            if (AiTools::tool_name(command, name, sizeof(name)) == 0)
            {
                continue;
            }

            uint32_t length = (command.help.size() < OPENAI_TOOL_DESCRIPTION_MAX) ? command.help.size() : OPENAI_TOOL_DESCRIPTION_MAX;
            memcpy(help, command.help.data(), length);
            help[length] = 0;

            uint32_t len = openai_tool(name, help, tool, sizeof(tool));

            if (len > 0)
            {
                if (!first)
                {
                    append(",", 1);
                }

                append(tool, len);
                first = false;
            }
        }

        append("]", 1);
    }

    append(",\"input\":[", 10);

    m_input_mark = m_body.seal();
}

bool OpenAIAgent::start(const SharedBuffer& prompt, OpenAIAgentDone done, void *context, const char *&why)
{
    if (!OpenAIManager::get().is_configured())
    {
        why = "OpenAI is not set up: the base URL on the settings page";
        return false;
    }

    if (running() || OpenAIManager::get().busy())
    {
        why = "already working on something - ai/stop ends it";
        return false;
    }

    if (prompt.empty())
    {
        why = "nothing was asked";
        return false;
    }

    m_prompt = prompt;
    AiLog::get().add("ask %.100s", prompt.c_str());
    AiSequence::get().stop();

    m_answer = SharedBuffer();
    m_topic[0] = 0;
    m_response_id[0] = 0;
    m_rounds = 0;
    m_calls = 0;
    m_calls_run = 0;
    m_retried = false;
    m_lost_calls = 0;
    m_started_us = m_now_us;
    m_done = done;
    m_context = context;

    const OpenAIConfig& config = OpenAIManager::get().get_config();

    if (!read_static_file(OPENAI_CONVERSATION_FILE, (uint8_t *)&m_conversation, sizeof(m_conversation)) ||
        strncmp(m_conversation.url, config.url, sizeof(m_conversation.url)) ||
        strncmp(m_conversation.model, config.model, sizeof(m_conversation.model)))
    {
        m_conversation.id[0] = 0;
    }

    m_conversation.id[sizeof(m_conversation.id) - 1] = 0;

    if (m_conversation.id[0] == 0)
    {
        if (!create())
        {
            why = m_why;
            finish("");
            m_state = State::Idle;
            return false;
        }

        trace("openai agent: started, making a conversation first: %.200s", prompt.c_str());
        return true;
    }

    first_request();

    if (m_full)
    {
        why = "the request does not fit in memory";
        finish("");
        m_state = State::Idle;
        return false;
    }

    if (!ask())
    {
        why = m_why;
        finish("");
        m_state = State::Idle;
        return false;
    }

    return true;
}

void OpenAIAgent::first_request()
{
    begin_request();

    append("{\"role\":\"developer\",\"content\":\"", 31);
    append_escaped(BASE_INSTRUCTIONS, strlen(BASE_INSTRUCTIONS));

    if (AiTools::instructions() != NULL)
    {
        append_escaped(" ", 1);
        append_escaped(AiTools::instructions(), strlen(AiTools::instructions()));
    }

    if (!m_instructions.empty())
    {
        append_escaped("\n\n", 2);
        append_escaped(m_instructions.c_str(), m_instructions.size());
    }

    if (m_tools_enabled && (AiContextCommandSet::size() > 0))
    {
        static const char *NOW = "\n\nAs things are right now:\n\n";
        append_escaped(NOW, strlen(NOW));
        m_command.run_each(AiContextCommandSet::all(), OPENAI_AGENT_CONTEXT_BYTES);

        char names[96] = {};
        int at = 0;

        for (const Command& command : AiContextCommandSet::all())
        {
            if (at < (int)sizeof(names))
            {
                at += snprintf(&names[at], sizeof(names) - at, "%s%.*s", (at > 0) ? "," : "", (int)command.name.size(), command.name.data());
            }
        }

        AiLog::get().add("context %s", names);
    }

    append("\"},{\"role\":\"user\",\"content\":\"", 29);
    append_escaped(m_prompt.c_str(), m_prompt.size());
    append("\"}", 2);
}

bool OpenAIAgent::create()
{
    m_body = BufferChain(OPENAI_AGENT_MAX_REQUEST);
    m_full = false;

    if (!append("{}", 2) || !OpenAIManager::get().post(m_body, on_created, this, true))
    {
        m_why = m_full ? "no memory" : OpenAIManager::get().last_error();
        return false;
    }

    m_state = State::Creating;
    AiLog::get().add("create");
    return true;
}

void OpenAIAgent::on_created(bool ok, OpenAIResponseReader& answer, void *context)
{
    ((OpenAIAgent *)context)->created(ok, answer);
}

void OpenAIAgent::created(bool ok, OpenAIResponseReader& answer)
{
    if (m_state != State::Creating)
    {
        return;
    }

    const OpenAIConfig& config = OpenAIManager::get().get_config();

    if (!ok || (answer.id()[0] == 0))
    {
        trace("openai agent: no conversation made: %.300s", answer.error().c_str());
        finish("OpenAI did not start a conversation - see the board's log");
        return;
    }

    snprintf(m_conversation.id, sizeof(m_conversation.id), "%s", answer.id());
    snprintf(m_conversation.url, sizeof(m_conversation.url), "%s", config.url);
    snprintf(m_conversation.model, sizeof(m_conversation.model), "%s", config.model);

    if (!write_static_file(OPENAI_CONVERSATION_FILE, (uint8_t *)&m_conversation, sizeof(m_conversation)))
    {
        trace("openai agent: %s could not be kept, the next request makes another", m_conversation.id);
    }

    trace("openai agent: new conversation %s", m_conversation.id);

    first_request();

    if (m_full)
    {
        finish("stopped: the request does not fit in memory");
        return;
    }

    if (!ask())
    {
        set_answer("the question could not be sent: %s", m_why);
        finish(m_answer.c_str());
    }
}

bool OpenAIAgent::forget()
{
    if (running())
    {
        return false;
    }

    memset(&m_conversation, 0, sizeof(m_conversation));
    write_static_file(OPENAI_CONVERSATION_FILE, (uint8_t *)&m_conversation, sizeof(m_conversation));
    return true;
}

bool OpenAIAgent::ask()
{
    if (!m_closed)
    {
        append("]}", 2);
        m_closed = true;
    }

    if (m_full)
    {
        m_why = "the request got too long";
        return false;
    }

    if (m_rounds == 0)
    {
        trace("openai agent: round 1 in %s, %u bytes with the instructions and context: %.300s",
              m_conversation.id, (unsigned int)m_body.size(), m_prompt.c_str());
    }
    else
    {
        trace("openai agent: round %u in %s, %u bytes: what %u calls answered",
              (unsigned int)(m_rounds + 1), m_conversation.id, (unsigned int)m_body.size(), (unsigned int)m_next_call);
    }

    if (!OpenAIManager::get().post(m_body, on_answer, this))
    {
        m_why = OpenAIManager::get().last_error();
        return false;
    }

    ++m_rounds;
    m_state = State::Asking;
    AiLog::get().add("send %u %u", (unsigned int)m_rounds, (unsigned int)m_body.size());
    return true;
}

void OpenAIAgent::on_answer(bool ok, OpenAIResponseReader& answer, void *context)
{
    ((OpenAIAgent *)context)->answered(ok, answer);
}

void OpenAIAgent::answered(bool ok, OpenAIResponseReader& answer)
{
    if (m_state != State::Asking)
    {
        return;     // stopped while the question was out
    }

    if (!ok)
    {
        if (answer.too_many_calls())
        {
            finish("stopped: it asked for more calls at once than the board takes");
            return;
        }

        static const char *NO_OUTPUT = "No tool output found for function call ";
        JsonView error(answer.error().c_str(), answer.error().size());
        char message[160] = {};

        if ((m_lost_calls < 8) && error.get_string("$.message", message, sizeof(message)) &&
            !strncmp(message, NO_OUTPUT, strlen(NO_OUTPUT)))
        {
            const char *id = &message[strlen(NO_OUTPUT)];
            uint32_t id_len = strcspn(id, ". ");
            char item[192];
            int len = snprintf(item, sizeof(item),
                               "{\"type\":\"function_call_output\",\"call_id\":\"%.*s\",\"output\":\"not run: the board lost this call\"},",
                               (int)id_len, id);

            if ((id_len > 0) && (len > 0) && (len < (int)sizeof(item)) && room_for(len) && m_body.insert(m_input_mark, item, len))
            {
                ++m_lost_calls;
                --m_rounds;

                trace("openai agent: answering lost call %.*s, asking again", (int)id_len, id);

                if (ask())
                {
                    return;
                }
            }
        }

        bool about_conversation = false;

        for (uint32_t i = 0; (i + 11) <= error.length(); ++i)
        {
            about_conversation = about_conversation || !memcmp(&error.data()[i], "onversation", 11);
        }

        if ((m_rounds == 1) && !m_retried && about_conversation)
        {
            m_retried = true;
            m_rounds = 0;
            m_answer = SharedBuffer();
            trace("openai agent: conversation %s refused, making a new one", m_conversation.id);

            if (create())
            {
                return;
            }
        }

        finish("OpenAI did not answer - see the board's log for why");
        return;
    }

    if (!answer.usable())
    {
        finish("OpenAI's answer could not be read - see the board's log");
        return;
    }

    snprintf(m_response_id, sizeof(m_response_id), "%s", answer.id());

    m_answer = answer.text();

    if (answer.calls() == 0)
    {
        trace("openai agent: round %u answered: %.300s", (unsigned int)m_rounds, m_answer.c_str());

        char started[160];

        if (AiSequence::get().start(m_answer.c_str(), started, sizeof(started)))
        {
            set_answer("%s", started);
        }

        finish(m_answer.c_str());
        return;
    }

    if ((m_rounds >= OPENAI_AGENT_MAX_ROUNDS) || ((m_calls_run + answer.calls()) > OPENAI_AGENT_MAX_CALLS))
    {
        finish(!m_answer.empty() ? m_answer.c_str() : "stopped: it took more steps than allowed");
        return;
    }

    free(m_call);
    m_call = answer.take_calls(m_calls);

    char line[320];
    int pos = 0;

    for (uint32_t i = 0; (i < m_calls) && (pos < (int)sizeof(line)); ++i)
    {
        pos += snprintf(&line[pos], sizeof(line) - pos, "%s%s(%s)", (i > 0) ? ", " : "", m_call[i].name, m_call[i].args);
    }

    trace("openai agent: round %u answered: %s%s%s", (unsigned int)m_rounds, line,
          !m_answer.empty() ? " - " : "", m_answer.c_str());
    AiLog::get().add("calls %u %s", (unsigned int)m_calls, line);

    begin_request();
    m_next_call = 0;
    m_state = State::Calling;
}

void OpenAIAgent::run_call()
{
    uint32_t i = m_next_call++;
    const Command *command = m_tools_enabled ? AiTools::find_tool(m_call[i].name) : NULL;

    if (i > 0)
    {
        append(",", 1);
    }

    append("{\"type\":\"function_call_output\",\"call_id\":\"", 42);
    append_escaped(m_call[i].id, strlen(m_call[i].id));
    append("\",\"output\":\"", 12);
    uint32_t answer_at = m_body.size();

    if ((command == NULL) || !AiTools::make_topic(*command, m_call[i].args, m_topic, sizeof(m_topic)))
    {
        append_escaped(AiTools::REFUSED, strlen(AiTools::REFUSED));
        trace("openai agent: call %u is %s(%s): refused, %s", (unsigned int)(m_calls_run + 1), m_call[i].name, m_call[i].args,
              (command == NULL) ? "no such tool" : "args too long");
        AiLog::get().add("call %s refused", m_call[i].name);
        m_topic[0] = 0;
    }
    else
    {
        trace("openai agent: call %u is %s(%s), running %s", (unsigned int)(m_calls_run + 1), m_call[i].name, m_call[i].args, m_topic);
        m_command.run(AiCommandSet::all(), m_topic, OPENAI_AGENT_REPLY_BYTES);

        if (m_command.written == 0)
        {
            append_escaped(AiTools::NO_ANSWER, strlen(AiTools::NO_ANSWER));
        }

        AiLog::get().add("call %s %u", m_topic, (unsigned int)m_command.written);
    }

    ++m_calls_run;

    char shown[300];
    uint32_t shown_len = m_body.copy_out(answer_at, shown, sizeof(shown));
    trace("openai agent: call %u %s(%s): %.*s", (unsigned int)m_calls_run, m_call[i].name, m_call[i].args, (int)shown_len, shown);

    append("\"}", 2);
}

void OpenAIAgent::finish(const char *text)
{
    if (text != m_answer.c_str())
    {
        set_answer("%s", (text[0] != 0) ? text : "(no answer)");
    }
    else if (m_answer.empty())
    {
        set_answer("(no answer)");
    }

    m_body = BufferChain();
    free(m_call);
    m_call = NULL;
    m_calls = 0;
    m_state = State::Delivering;
    m_delivering_us = m_now_us;

    trace("openai agent: finished after %u rounds, %u calls: %.300s",
          (unsigned int)m_rounds, (unsigned int)m_calls_run, m_answer.c_str());
    AiLog::get().add("end %s", m_answer.c_str());
}

void OpenAIAgent::stop(const char *why)
{
    if (!running())
    {
        return;
    }

    trace("openai agent: stopped: %s", why);

    set_answer("stopped: %s", why);
    finish(m_answer.c_str());
}

void OpenAIAgent::update(uint64_t now_us)
{
    m_now_us = now_us;

    if (((m_state == State::Asking) || (m_state == State::Creating) || (m_state == State::Calling)) &&
        ((now_us - m_started_us) > (OPENAI_AGENT_TIMEOUT_S * 1000000ULL)))
    {
        stop("it took too long");
        return;
    }

    if (m_state == State::Calling)
    {
        if (m_next_call < m_calls)
        {
            run_call();

            if (m_full)
            {
                finish("stopped: the conversation got too long");
            }

            return;
        }

        if (!OpenAIManager::get().busy() && !ask())
        {
            set_answer("the next question could not be sent: %s", m_why);
            finish(m_answer.c_str());
        }

        return;
    }

    if (m_state == State::Delivering)
    {
        bool delivered = (m_done == NULL) || m_done(m_answer.c_str(), m_context);

        if (delivered || ((now_us - m_delivering_us) > DELIVER_TIMEOUT_US))
        {
            if (!delivered)
            {
                trace("openai agent: answer could not be delivered, kept for ai/status");
            }

            m_state = State::Idle;
            m_done = NULL;
            m_context = NULL;
        }
    }
}

uint32_t OpenAIAgent::status(char *out, uint32_t size) const
{
    int len = 0;

    switch (m_state)
    {
        case State::Idle:
            len = snprintf(out, size, "idle%s%s", !m_answer.empty() ? "\n" : "", m_answer.c_str());
            break;
        case State::Creating:
            len = snprintf(out, size, "starting a conversation with OpenAI");
            break;
        case State::Asking:
            len = snprintf(out, size, "asking OpenAI, round %u, %u tool calls so far", (unsigned int)m_rounds, (unsigned int)m_calls_run);
            break;
        case State::Calling:
            len = snprintf(out, size, "running tools, last %s, %u calls so far", m_topic, (unsigned int)m_calls_run);
            break;
        case State::Delivering:
            len = snprintf(out, size, "done\n%s", m_answer.c_str());
            break;
    }

    return (len < 0) ? 0 : (((uint32_t)len < size) ? (uint32_t)len : size - 1);
}
