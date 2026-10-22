/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#pragma once

#include "command_dispatcher.h"
#include "command_registration.h"
#include "telegram_keyboard.h"
#include "telegram_updates.h"

class TelegramCommandContext : public CommandContext
{
public:
    uint32_t send_space() override;

protected:
    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;

private:
    bool m_stream_started = false;
};

class TelegramCommands : public ITelegramUpdates
{
public:
    static TelegramCommands& get();

    static void enable(const char *title, const TelegramButton *buttons, uint32_t count, uint32_t columns = 2);

    void on_telegram_update(const TelegramUpdate& update) override;
    bool busy() const override { return !m_dispatcher.idle(); }
    void abort(const char *reason) override { m_dispatcher.abort(reason); }

private:
    TelegramCommands() : m_dispatcher(TelegramCommandSet::all(), m_context, m_slots) {}

    TelegramCommandContext m_context;
    CommandSlot m_slots[1];
    CommandDispatcher m_dispatcher;
};
