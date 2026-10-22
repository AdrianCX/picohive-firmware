/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_table.h"

// Declare mqtt_prefix / ws_prefix / telegram_prefix only to differ from these.
COMMAND_TRAIT(mqtt_prefix_of, mqtt_prefix, std::string_view, std::string_view("send/"))
COMMAND_TRAIT(ws_prefix_of, ws_prefix, std::string_view, std::string_view("/rc/"))
COMMAND_TRAIT(telegram_prefix_of, telegram_prefix, std::string_view, std::string_view("/"))

// gnu::used and KEEP() in commands.ld keep the entry nothing references;
// gnu::aligned stops the compiler padding entries apart. The extern "C"
// function makes the same class registered twice a link error.
#define COMMAND_ENTRY(CLASS, TRANSPORT, SECTION, PREFIX)                                        \
    namespace                                                                                   \
    {                                                                                           \
        [[gnu::used, gnu::section(SECTION), gnu::aligned(alignof(Command))]]                    \
        const Command CLASS##_##TRANSPORT##_command_entry = CommandEntry<CLASS>::make(PREFIX);  \
    }                                                                                           \
    extern "C" void TRANSPORT##_command_##CLASS() {}

#define MQTT_COMMAND(CLASS)                                                                     \
    COMMAND_ENTRY(CLASS, mqtt, "mqtt_commands", mqtt_prefix_of<CLASS>())

#define WS_COMMAND(CLASS)                                                                       \
    COMMAND_ENTRY(CLASS, ws, "ws_commands", ws_prefix_of<CLASS>())

// Typed in the telegram chat, or the callback data of a keyboard button
#define TELEGRAM_COMMAND(CLASS)                                                                 \
    COMMAND_ENTRY(CLASS, telegram, "telegram_commands", telegram_prefix_of<CLASS>())

// The title of a calendar event, "<device id>/water/1/10", once it starts.
// The id is stripped before the lookup, so there is no prefix: the command is
// found by its name alone. See pico_calendar_commands.
#define CALENDAR_COMMAND(CLASS)                                                                 \
    COMMAND_ENTRY(CLASS, calendar, "calendar_commands", std::string_view())

// A tool OpenAI may call, see pico_openai/openai_agent.h. The model sends the
COMMAND_TRAIT(ai_help_of, ai_help, std::string_view, help_of<Class>())

template <typename Class>
consteval Command ai_command_entry()
{
    static_assert(payload_of<Class>() != Payload::Streamed, "AI_COMMAND: a tool call has no body to stream, only args");

    if constexpr (requires { Class::ai_help; })
    {
        static_assert(Class::ai_help.size() <= 64, "ai_help: 64 characters at most, the model is sent no more");
    }

    Command command = CommandEntry<Class>::make(std::string_view());
    command.help = ai_help_of<Class>();
    return command;
}

#define AI_COMMAND(CLASS)                                                                       \
    namespace                                                                                   \
    {                                                                                           \
        [[gnu::used, gnu::section("ai_commands"), gnu::aligned(alignof(Command))]]              \
        const Command CLASS##_ai_command_entry = ai_command_entry<CLASS>();                     \
    }                                                                                           \
    extern "C" void ai_command_##CLASS() {}

// Run with no args when an ai request starts, its answer handed to the model
// up front - the state of things it would otherwise ask for first.
#define AI_CONTEXT(CLASS)                                                                       \
    namespace                                                                                   \
    {                                                                                           \
        [[gnu::used, gnu::section("ai_context_commands"), gnu::aligned(alignof(Command))]]      \
        const Command CLASS##_ai_context_entry = ai_command_entry<CLASS>();                     \
    }                                                                                           \
    extern "C" void ai_context_##CLASS() {}

extern "C"
{
    extern const Command __start_mqtt_commands[];
    extern const Command __stop_mqtt_commands[];
    extern const Command __start_ws_commands[];
    extern const Command __stop_ws_commands[];
    extern const Command __start_telegram_commands[];
    extern const Command __stop_telegram_commands[];
    extern const Command __start_calendar_commands[];
    extern const Command __stop_calendar_commands[];
    extern const Command __start_ai_commands[];
    extern const Command __stop_ai_commands[];
    extern const Command __start_ai_context_commands[];
    extern const Command __stop_ai_context_commands[];
}

using MqttCommandSet = LinkedCommandSet<__start_mqtt_commands, __stop_mqtt_commands>;
using WsCommandSet = LinkedCommandSet<__start_ws_commands, __stop_ws_commands>;
using TelegramCommandSet = LinkedCommandSet<__start_telegram_commands, __stop_telegram_commands>;
using CalendarCommandSet = LinkedCommandSet<__start_calendar_commands, __stop_calendar_commands>;
using AiCommandSet = LinkedCommandSet<__start_ai_commands, __stop_ai_commands>;
using AiContextCommandSet = LinkedCommandSet<__start_ai_context_commands, __stop_ai_context_commands>;
