/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "pico_logger.h"
#include "pico_mjson.h"
#include "telegram_updates.h"

bool decode_telegram_updates(const char *data, uint32_t len, ITelegramUpdates *sink, uint32_t& highest_update_id, TelegramUpdate& update)
{
    highest_update_id = 0;

    JsonView root(data, len);

    if (!root.is_valid() || (root.type() != JsonType::Object))
    {
        trace("telegram: getUpdates answer is not json (%u bytes)", len);
        return false;
    }

    bool ok = false;

    if (!root.get_bool("$.ok", ok) || !ok)
    {
        JsonView why = root.find("$.description");

        if (why.length() == 0)
        {
            why = JsonView("no reason given");
        }

        trace("telegram: getUpdates refused: %.*s", (int)why.length(), why.data());
        return false;
    }

    JsonView result = root.find("$.result");

    if (result.type() != JsonType::Array)
    {
        return false;
    }

    for (const JsonEntry& entry : result)
    {
        const JsonView& item = entry.value;

        update.from_button = false;
        update.chat_id[0] = 0;
        update.callback_id[0] = 0;
        update.text[0] = 0;

        if (!item.get_uint("$.update_id", update.update_id))
        {
            continue;
        }

        if (update.update_id > highest_update_id)
        {
            highest_update_id = update.update_id;
        }

        if (item.type("$.callback_query") == JsonType::Object)
        {
            update.from_button = true;
            item.get_string("$.callback_query.data", update.text, sizeof(update.text));
            item.get_string("$.callback_query.id", update.callback_id, sizeof(update.callback_id));
            item.get_string("$.callback_query.message.chat.id", update.chat_id, sizeof(update.chat_id));
        }
        else if (item.type("$.message") == JsonType::Object)
        {
            item.get_string("$.message.text", update.text, sizeof(update.text));
            item.get_string("$.message.chat.id", update.chat_id, sizeof(update.chat_id));
        }
        else
        {
            continue;
        }

        if (update.text[0] == 0)
        {
            continue;
        }

        if (sink != NULL)
        {
            sink->on_telegram_update(update);
        }
    }

    return true;
}

void telegram_split_command(std::string_view text, std::string_view& command, std::string_view& body)
{
    size_t split = text.find_first_of(" \n\t");

    command = (split == std::string_view::npos) ? text : text.substr(0, split);
    body = (split == std::string_view::npos) ? std::string_view() : text.substr(split + 1);

    // "/start@MyBot" - telegram adds the bot name in group chats.
    size_t mention = command.find('@');

    if (mention != std::string_view::npos)
    {
        command = command.substr(0, mention);
    }
}
