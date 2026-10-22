/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>
#include <string_view>

#ifndef MAX_TELEGRAM_TEXT
#define MAX_TELEGRAM_TEXT 256
#endif

#define MAX_TELEGRAM_CALLBACK_ID 64
#define MAX_TELEGRAM_ID 32

struct TelegramUpdate
{
    uint32_t update_id = 0;
    bool from_button = false;
    char chat_id[MAX_TELEGRAM_ID] = {};
    char callback_id[MAX_TELEGRAM_CALLBACK_ID] = {};
    char text[MAX_TELEGRAM_TEXT] = {};
};

class ITelegramUpdates
{
public:
    virtual ~ITelegramUpdates() = default;
    virtual void on_telegram_update(const TelegramUpdate& update) = 0;

    virtual bool busy() const { return false; }
    virtual void abort(const char *reason) {}
};

bool decode_telegram_updates(const char *data, uint32_t len, ITelegramUpdates *sink, uint32_t& highest_update_id, TelegramUpdate& update);
void telegram_split_command(std::string_view text, std::string_view& command, std::string_view& body);
