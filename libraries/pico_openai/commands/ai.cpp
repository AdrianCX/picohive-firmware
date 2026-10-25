/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "ai.h"
#include "ai_log.h"
#include "command_mqtt_manager.h"
#include "openai_agent.h"
#include "openai_sequence.h"

#if __has_include("telegram_manager.h")
#include "telegram_manager.h"
#define AI_HAS_TELEGRAM 1
#endif

static bool to_mqtt(const char *text, void *)
{
    return CommandMqttManager::get().publish_text("ai", text, strlen(text));
}

#ifdef AI_HAS_TELEGRAM
static bool to_telegram(const char *text, void *)
{
    return TelegramManager::get().queue_reply(text, strlen(text));
}
#endif

bool Ai::on_payload(CommandContext& ctx, std::string_view body)
{
    std::string_view text = body.empty() ? ctx.args : body;

    ReplyBuffer prompt(ctx, max_bytes);
    if (!prompt)
    {
        return ctx.reply("no memory for the request");
    }

    prompt.append(text);

    for (char *c = prompt.data(); body.empty() && (*c != 0); ++c)
    {
        if (*c == '/')
        {
            *c = ' ';
        }
    }

    if (prompt.empty())
    {
        return ctx.reply("ai what? e.g. /ai <what to do, in plain words>");
    }

    OpenAIAgentDone done = NULL;

    if (ctx.prefix == "send/")
    {
        done = to_mqtt;
    }
#ifdef AI_HAS_TELEGRAM
    else if (ctx.prefix == "/")
    {
        done = to_telegram;
    }
#endif

    const char *why = NULL;

    if (!OpenAIAgent::get().start(prompt, done, NULL, why))
    {
        return ctx.reply_fmt(96, "not started: %s", why);
    }

    return ctx.reply(prompt);
}

bool AiStatus::on_header(CommandContext& ctx)
{
    ReplyBuffer text(ctx, OpenAIAgent::get().answer_size() + 255);
    if (!text)
    {
        return ctx.reply("no memory for the reply");
    }

    text.set_size(OpenAIAgent::get().status(text.data(), text.capacity() + 1));

    if (AiSequence::get().running() && text.append("\n"))
    {
        uint32_t len = text.size();
        text.set_size(len + AiSequence::get().status(text.data() + len, text.capacity() + 1 - len));
    }

    return ctx.reply(text);
}

bool AiEvents::on_header(CommandContext& ctx)
{
    uint32_t after = 0;

    for (char c : ctx.args)
    {
        if ((c < '0') || (c > '9'))
        {
            break;
        }

        after = (after * 10) + (uint32_t)(c - '0');
    }

    ReplyBuffer text(ctx, (AI_LOG_LINES * (AI_LOG_LINE_BYTES + 24)) + 32);
    if (!text)
    {
        return ctx.reply("no memory for the reply");
    }

    bool running = OpenAIAgent::get().running() || AiSequence::get().running();
    text.append_snprintf("%s %u\n", running ? "running" : "idle", (unsigned int)AiLog::get().last());

    uint32_t len = text.size();
    text.set_size(len + AiLog::get().after(after, text.data() + len, text.capacity() + 1 - len));

    return ctx.reply(text);
}

bool AiStop::on_header(CommandContext& ctx)
{
    bool asking = OpenAIAgent::get().running();
    bool playing = AiSequence::get().running();

    if (!asking && !playing)
    {
        return ctx.reply("nothing running");
    }

    OpenAIAgent::get().stop("asked to");
    AiSequence::get().stop();
    return ctx.reply("stopped");
}

bool AiNew::on_header(CommandContext& ctx)
{
    return ctx.reply(OpenAIAgent::get().forget() ? "the next request starts a new conversation" : "busy - ai/stop first");
}

MQTT_COMMAND(Ai);
WS_COMMAND(Ai);
TELEGRAM_COMMAND(Ai);

MQTT_COMMAND(AiStatus);
WS_COMMAND(AiStatus);
TELEGRAM_COMMAND(AiStatus);

MQTT_COMMAND(AiNew);
WS_COMMAND(AiNew);
TELEGRAM_COMMAND(AiNew);

MQTT_COMMAND(AiEvents);
WS_COMMAND(AiEvents);

MQTT_COMMAND(AiStop);
WS_COMMAND(AiStop);
TELEGRAM_COMMAND(AiStop);

void openai_agent_load_config(const JsonView& config)
{
    OpenAIAgent::get().load_config(config);
}

void openai_agent_update(uint64_t now_us)
{
    AiLog::get().set_now(now_us);
    OpenAIAgent::get().update(now_us);
    AiSequence::get().update(now_us);
}
