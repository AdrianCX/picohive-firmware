/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "ai_tools.h"
#include "command_dispatcher.h"
#include "ai_log.h"
#include "command_registration.h"
#include "openai_sequence.h"
#include "pico_logger.h"

bool SequenceCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    trace("ai sequence: %.*s: %.*s", (int)topic.size(), topic.data(), (int)((len < 200) ? len : 200), (const char *)data);
    return true;
}

bool SequenceCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    trace("ai sequence: %.*s answers %u bytes, not read", (int)topic.size(), topic.data(), (unsigned int)total_bytes);
    return true;
}

bool SequenceCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    return true;
}

AiSequence& AiSequence::get()
{
    static AiSequence instance;
    return instance;
}

bool AiSequence::load(const char *answer, uint32_t& steps, uint32_t& total_ms, char *why, uint32_t why_size)
{
    steps = 0;
    total_ms = 0;
    why[0] = 0;

    const char *open = strchr(answer, '{');
    const char *close = strrchr(answer, '}');

    if ((open == NULL) || (close == NULL) || (close < open))
    {
        return false;
    }

    JsonView plan(open, (uint32_t)(close - open + 1));

    if (!plan.is_valid() || (plan.type("$.steps") != JsonType::Array))
    {
        return false;
    }

    stop();

    AiTools::set_busy([]() { return AiSequence::get().busy(); });

    uint32_t room = (plan.length() < AI_SEQUENCE_BYTES) ? plan.length() : AI_SEQUENCE_BYTES;
    m_commands = SharedBuffer(room);

    if (!m_commands)
    {
        trace("ai sequence: no memory for a %u byte plan", (unsigned int)room);
        return false;
    }

    char *commands = m_commands.data();
    uint32_t used = 0;

    for (const JsonEntry& entry : plan.find("$.steps"))
    {
        const JsonView& step = entry.value;
        int64_t wait = 0;

        if ((m_count >= AI_SEQUENCE_MAX_STEPS) || (step.type("$.do") != JsonType::String) ||
            !step.get_string("$.do", &commands[used], room + 1 - used))
        {
            trace("ai sequence: step %u left out: too many, too long, or no \"do\"", (unsigned int)(m_count + 1));
            continue;
        }

        step.get_int("$.ms", wait);

        int64_t wait_done = 1;
        step.get_int("$.w", wait_done);
        wait = (wait < 0) ? 0 : ((wait > AI_SEQUENCE_MAX_WAIT_MS) ? AI_SEQUENCE_MAX_WAIT_MS : wait);

        char *command = &commands[used];
        uint32_t skip = (strncmp(command, "/rc/", 4) == 0) ? 4 : ((command[0] == '/') ? 1 : 0);
        memmove(command, &command[skip], strlen(command) - skip + 1);

        m_at[m_count] = (uint16_t)used;
        m_wait_ms[m_count] = (uint16_t)wait;
        m_wait_done[m_count] = (wait_done != 0);
        total_ms += (uint32_t)wait;
        used += strlen(command) + 1;
        ++m_count;
    }

    plan.get_string("$.why", why, why_size);

    if (m_count == 0)
    {
        m_commands = SharedBuffer();
    }

    steps = m_count;
    m_index = 0;
    m_next_us = m_now_us;

    trace("ai sequence: %u steps, about %u ms%s%s", (unsigned int)m_count, (unsigned int)total_ms, (why[0] != 0) ? ": " : "", why);
    return true;
}

bool AiSequence::start(const char *text, char *out, uint32_t size)
{
    uint32_t steps = 0;
    uint32_t total_ms = 0;
    char why[128];

    if (!load(text, steps, total_ms, why, sizeof(why)))
    {
        return false;
    }

    if (steps == 0)
    {
        snprintf(out, size, "nothing to do: %s", (why[0] != 0) ? why : "no steps");
        return true;
    }

    if (total_ms == 0)
    {
        snprintf(out, size, "running %u steps", (unsigned int)steps);
    }
    else
    {
        snprintf(out, size, "running %u steps, about %u.%u s", (unsigned int)steps,
                 (unsigned int)(total_ms / 1000), (unsigned int)((total_ms % 1000) / 100));
    }
    AiLog::get().add("plan %u %u", (unsigned int)steps, (unsigned int)total_ms);
    return true;
}

void AiSequence::stop()
{
    if (running())
    {
        trace("ai sequence: stopped at step %u of %u", (unsigned int)(m_index + 1), (unsigned int)m_count);
        AiLog::get().add("planstop %u/%u", (unsigned int)(m_index + 1), (unsigned int)m_count);
    }

    m_count = 0;
    m_index = 0;
    m_commands = SharedBuffer();
}

void AiSequence::update(uint64_t now_us)
{
    m_now_us = now_us;

    if (!running() || (now_us < m_next_us))
    {
        return;
    }

    if (AiTools::has_ready() && (m_index > 0) && m_wait_done[m_index - 1])
    {
        uint64_t since = now_us - m_step_us;

        if (!AiTools::done_after(since, AI_SEQUENCE_READY_TIMEOUT_US))
        {
            return;
        }

        if (since >= AI_SEQUENCE_READY_TIMEOUT_US)
        {
            trace("ai sequence: step %u still not done after %u s, going on", (unsigned int)m_index, (unsigned int)(since / 1000000));
        }
    }

    run_step();
}

void AiSequence::run_step()
{
    const char *command = &m_commands.c_str()[m_at[m_index]];
    std::string_view args;

    AiLog::get().add("step %u/%u %s", (unsigned int)(m_index + 1), (unsigned int)m_count, command);

    if (AiCommandSet::find(command, args) == NULL)
    {
        trace("ai sequence: step %u of %u, %s: not a command it may run, skipped", (unsigned int)(m_index + 1), (unsigned int)m_count, command);
    }
    else
    {
        trace("ai sequence: step %u of %u: %s, then done%s and %u ms", (unsigned int)(m_index + 1), (unsigned int)m_count, command,
              AiTools::has_ready() ? " moving" : "", (unsigned int)m_wait_ms[m_index]);

        CommandDispatcher::run_once(AiCommandSet::all(), m_command, std::string_view(command));
    }

    m_step_us = m_now_us;
    m_next_us = m_now_us + (m_wait_ms[m_index] * 1000ULL);
    ++m_index;

    if (!running())
    {
        trace("ai sequence: done");
        AiLog::get().add("plandone");
        m_commands = SharedBuffer();
    }
}

bool AiSequence::busy() const
{
    return (m_count > 0) && (running() || !AiTools::done_after(m_now_us - m_step_us, AI_SEQUENCE_READY_TIMEOUT_US));
}

uint32_t AiSequence::status(char *out, uint32_t size) const
{
    int len = running()
        ? snprintf(out, size, "step %u of %u: %s", (unsigned int)(m_index + 1), (unsigned int)m_count, &m_commands.c_str()[m_at[m_index]])
        : snprintf(out, size, "no plan running");

    return (len < 0) ? 0 : (((uint32_t)len < size) ? (uint32_t)len : size - 1);
}
