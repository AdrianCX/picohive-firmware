/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_table.h"

#ifndef MAX_COMMAND_ARGS
#define MAX_COMMAND_ARGS 64
#endif

struct CommandSlot
{
    const Command *command = nullptr;
    uint32_t ticket = 0;
    bool clearing = false;
    bool more_to_send = false;
    uint32_t message_length = 0;
    uint32_t received = 0;
    uint16_t args_len = 0;
    char args[MAX_COMMAND_ARGS] = {};

    alignas(std::max_align_t) unsigned char storage[MAX_COMMAND_INSTANCE_BYTES];
};

class CommandDispatcher
{
public:
    CommandDispatcher(std::span<const Command> table, CommandContext& context, std::span<CommandSlot> slots);
    ~CommandDispatcher();

    static bool run_once(std::span<const Command> table, CommandContext& context, std::string_view topic,
                         std::string_view body = std::string_view());

    std::span<const Command> table() const { return m_table; }

    bool is_streaming() const { return m_active != nullptr; }
    bool is_sending() const { return m_sending != nullptr; }

    bool has_queued() const { return m_context.has_queued(); }
    bool idle() const;
    bool is_exclusive() const;

    bool dispatch_header(std::string_view topic, uint32_t payload_size);
    bool dispatch_data(const uint8_t *data, uint32_t len, bool last_chunk);
    bool dispatch_sent();

    void retained_cleared(uint32_t ticket);
    void abandon(uint32_t ticket, const char *reason);

    void abort(const char *reason);

private:
    bool finish_retained(CommandSlot *slot);
    void describe(const Command *command, std::string_view args, uint32_t payload_size);
    void bind(CommandSlot *slot);
    void rebind(CommandSlot *slot);
    void settle(CommandSlot *slot);
    bool pump_sending(CommandSlot *slot);
    void abort_slot(CommandSlot *slot, const char *reason);
    CommandSlot *exclusive_slot() const;
    void release(CommandSlot *slot);
    CommandSlot *take_slot();
    CommandSlot *find_ticket(uint32_t ticket);

    std::span<const Command> m_table;
    CommandContext& m_context;
    std::span<CommandSlot> m_slots;

    CommandSlot *m_active = nullptr;    // receiving a body
    CommandSlot *m_sending = nullptr;   // sending a reply
    CommandSlot *m_bound = nullptr;     // whose message m_context describes
    uint32_t m_last_ticket = NO_WAITER;
};
