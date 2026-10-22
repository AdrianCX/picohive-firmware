/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#include <string.h>

#include "pico_logger.h"
#include "telegram_commands.h"
#include "telegram_manager.h"

bool TelegramCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    return TelegramManager::get().queue_reply((const char *)data, len);
}

bool TelegramCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    m_stream_started = false;
    return true;
}

uint32_t TelegramCommandContext::send_space()
{
    return UINT32_MAX;
}

bool TelegramCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    TelegramManager::get().queue_reply((const char *)data, len, !m_stream_started);
    m_stream_started = true;
    return true;
}

TelegramCommands& TelegramCommands::get()
{
    static TelegramCommands instance;
    return instance;
}

void TelegramCommands::enable(const char *title, const TelegramButton *buttons, uint32_t count, uint32_t columns)
{
    TelegramManager& telegram = TelegramManager::get();

    telegram.set_keyboard(title, buttons, count, columns);
    telegram.set_update_sink(&get());
}

void TelegramCommands::on_telegram_update(const TelegramUpdate& update)
{
    TelegramManager& telegram = TelegramManager::get();

    if (!telegram.chat_allowed(update.chat_id))
    {
        trace("telegram: update %u from chat %s ignored", (unsigned int)update.update_id, update.chat_id);
        return;
    }

    if (update.from_button)
    {
        telegram.queue_callback_answer(update.callback_id);
    }

    std::string_view command;
    std::string_view body;
    telegram_split_command(update.text, command, body);

    std::string_view args;

    if (TelegramCommandSet::find(command, args) == NULL)
    {
        char message[MAX_TELEGRAM_TEXT + 32];
        int len = snprintf(message, sizeof(message), "%.*s? try /start", (int)command.size(), command.data());

        if (len > 0)
        {
            telegram.queue_reply(message, (len < (int)sizeof(message)) ? len : (int)sizeof(message) - 1);
        }

        return;
    }

    if (!m_dispatcher.dispatch_header(command, body.size()))
    {
        return;
    }

    if (m_dispatcher.is_streaming() && !body.empty())
    {
        m_dispatcher.dispatch_data((const uint8_t *)body.data(), body.size(), true);
    }
}
