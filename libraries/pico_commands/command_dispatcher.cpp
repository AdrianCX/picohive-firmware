/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"

#include "command_dispatcher.h"

const Command *find_command(std::span<const Command> table, std::string_view topic, std::string_view& args)
{
    const Command *match = nullptr;
    size_t matched = 0;

    for (const Command& command : table)
    {
        if (!topic.starts_with(command.prefix))
        {
            continue;
        }

        std::string_view rest = topic.substr(command.prefix.size());

        if (!rest.starts_with(command.name) || ((rest.size() != command.name.size()) && (rest[command.name.size()] != '/')))
        {
            continue;
        }

        if ((match == nullptr) || (command.name.size() > match->name.size()))
        {
            match = &command;
            matched = command.prefix.size() + command.name.size();
        }
    }

    args = ((match != nullptr) && (topic.size() > matched)) ? topic.substr(matched + 1) : std::string_view();
    return match;
}

CommandDispatcher::CommandDispatcher(std::span<const Command> table, CommandContext& context, std::span<CommandSlot> slots)
    : m_table(table), m_context(context), m_slots(slots)
{
}

CommandDispatcher::~CommandDispatcher()
{
    abort("the dispatcher is going away");
}

bool CommandDispatcher::run_once(std::span<const Command> table, CommandContext& context, std::string_view topic,
                                 std::string_view body)
{
    CommandSlot slots[1];
    CommandDispatcher dispatcher(table, context, slots);

    if (!dispatcher.dispatch_header(topic, body.size()))
    {
        return false;
    }

    return body.empty() || dispatcher.dispatch_data((const uint8_t *)body.data(), body.size(), true);
}

bool CommandDispatcher::idle() const
{
    for (const CommandSlot& slot : m_slots)
    {
        if (slot.command != nullptr)
        {
            return false;
        }
    }

    return !m_context.has_queued();
}

bool CommandDispatcher::is_exclusive() const
{
    return exclusive_slot() != nullptr;
}

CommandSlot *CommandDispatcher::exclusive_slot() const
{
    for (const CommandSlot& slot : m_slots)
    {
        if ((slot.command != nullptr) && slot.command->exclusive)
        {
            return const_cast<CommandSlot *>(&slot);
        }
    }

    return nullptr;
}

bool CommandDispatcher::dispatch_header(std::string_view topic, uint32_t payload_size)
{
    const CommandSlot *exclusive = exclusive_slot();
    if (exclusive != nullptr)
    {
        trace("commands: ignoring %.*s, %.*s is in flight with %u of %u bytes in",(int)topic.size(), topic.data(), (int)exclusive->command->name.size(), exclusive->command->name.data(), exclusive->received, exclusive->message_length);
        return true;
    }

    if (!m_context.stream_open())
    {
        m_context.flush_queued();
    }

    if (m_active != nullptr)
    {
        abort_slot(m_active, "another command arrived");
    }


    std::string_view args;
    const Command *command = find_command(m_table, topic, args);

    if (command == nullptr)
    {
        return true;
    }

    if ((m_sending != nullptr) && !command->silent)
    {
        if ((command->on_send_space != nullptr) || command->exclusive || command->clears_retained || !m_context.can_queue())
        {
            abort_slot(m_sending, "another command arrived");
        }
    }

    if ((command->max_bytes != 0) && (payload_size > command->max_bytes))
    {
        describe(command, args, payload_size);
        m_context.on_payload_too_large(payload_size, command->max_bytes);

        if (command->clears_retained)
        {
            m_context.clear_retained(NO_WAITER);
        }

        return true;
    }

    if (command->instance_size > MAX_COMMAND_INSTANCE_BYTES)
    {
        return true;
    }

    CommandSlot *slot = take_slot();
    if (slot == nullptr)
    {
        return true;
    }

    args = args.substr(0, MAX_COMMAND_ARGS - 1);
    if (!args.empty())
    {
        memcpy(slot->args, args.data(), args.size());
    }
    slot->args[args.size()] = 0;
    slot->args_len = args.size();

    if (++m_last_ticket == NO_WAITER)
    {
        ++m_last_ticket;
    }

    slot->command = command;
    slot->ticket = m_last_ticket;
    slot->clearing = false;
    slot->more_to_send = false;
    slot->message_length = payload_size;
    slot->received = 0;

    command->construct(slot->storage);
    bind(slot);

    if (command->on_data != nullptr)
    {
        m_active = slot;
    }

    if ((command->on_header != nullptr) && !command->on_header(slot->storage, m_context))
    {
        abort_slot(slot, "the header could not be handled");
        return false;
    }

    if (m_context.dropped_send)
    {
        trace("commands: %.*s is silent, its answer was dropped", (int)command->name.size(), command->name.data());
        m_context.dropped_send = false;
    }

    if ((m_active == slot) && (payload_size == 0))
    {
        static const uint8_t nothing = 0;
        return dispatch_data(&nothing, 0, true);
    }

    settle(slot);
    return true;
}

bool CommandDispatcher::dispatch_data(const uint8_t *data, uint32_t len, bool last_chunk)
{
    CommandSlot *slot = m_active;

    if (slot == nullptr)
    {
        return true;
    }

    bind(slot);

    if (last_chunk)
    {
        m_active = nullptr;
    }

    slot->received += len;

    if (!slot->command->on_data(slot->storage, m_context, data, len, last_chunk))
    {
        abort_slot(slot, "the command refused the data");
        return false;
    }

    if (last_chunk && slot->command->clears_retained && !finish_retained(slot))
    {
        return true;
    }

    settle(slot);
    return true;
}

bool CommandDispatcher::dispatch_sent()
{
    CommandSlot *slot = m_sending;

    if ((slot != nullptr) && m_context.stream_open())
    {
        return pump_sending(slot);
    }

    if (!m_context.flush_queued())
    {
        return false;
    }

    if (m_context.has_queued())
    {
        return true;
    }

    return (slot != nullptr) ? pump_sending(slot) : true;
}

bool CommandDispatcher::pump_sending(CommandSlot *slot)
{
    bind(slot);

    if (!slot->command->on_send_space(slot->storage, m_context))
    {
        abort_slot(slot, "the command failed while sending");
        return false;
    }

    settle(slot);
    return true;
}

void CommandDispatcher::retained_cleared(uint32_t ticket)
{
    CommandSlot *slot = find_ticket(ticket);

    if ((slot == nullptr) || !slot->clearing)
    {
        return;
    }

    CommandSlot *previous = m_bound;

    slot->clearing = false;
    bind(slot);

    if (slot->command->on_retained_cleared != nullptr)
    {
        slot->command->on_retained_cleared(slot->storage, m_context);
    }

    settle(slot);

    rebind(previous);
}

void CommandDispatcher::abandon(uint32_t ticket, const char *reason)
{
    CommandSlot *slot = find_ticket(ticket);

    if (slot == nullptr)
    {
        return;
    }

    CommandSlot *previous = m_bound;
    abort_slot(slot, reason);
    rebind(previous);
}

void CommandDispatcher::abort(const char *reason)
{
    m_context.drop_queued();

    for (CommandSlot& slot : m_slots)
    {
        if (slot.command == nullptr)
        {
            continue;
        }

        abort_slot(&slot, reason);
    }
}

bool CommandDispatcher::finish_retained(CommandSlot *slot)
{
    const Command *command = slot->command;

    if (!command->exclusive)
    {
        if (slot->message_length > 0)
        {
            m_context.clear_retained(NO_WAITER);
        }

        return true;
    }

    ClearResult result = (slot->message_length == 0) ? ClearResult::Done : m_context.clear_retained(slot->ticket);

    switch (result)
    {
        case ClearResult::Done:
            if (command->on_retained_cleared != nullptr)
            {
                command->on_retained_cleared(slot->storage, m_context);
            }
            return true;

        case ClearResult::Pending:
            slot->clearing = true;
            return true;

        default:
            abort_slot(slot, "the retained message could not be cleared");
            return false;
    }
}

void CommandDispatcher::describe(const Command *command, std::string_view args, uint32_t payload_size)
{
    m_bound = nullptr;
    m_context.silenced = command->silent;
    m_context.dropped_send = false;
    m_context.defer_sends((m_sending != nullptr) || m_context.has_queued());
    m_context.prefix = command->prefix;
    m_context.name = command->name;
    m_context.args = args;
    m_context.message_length = payload_size;
    m_context.more_to_send = false;
}

void CommandDispatcher::bind(CommandSlot *slot)
{
    m_bound = slot;
    m_context.silenced = slot->command->silent;
    m_context.dropped_send = false;

    m_context.defer_sends(((m_sending != nullptr) && (m_sending != slot)) || m_context.has_queued());
    m_context.prefix = slot->command->prefix;
    m_context.name = slot->command->name;
    m_context.args = std::string_view(slot->args, slot->args_len);
    m_context.message_length = slot->message_length;
    m_context.more_to_send = slot->more_to_send;
}

void CommandDispatcher::rebind(CommandSlot *slot)
{
    if ((slot != nullptr) && (slot->command != nullptr))
    {
        bind(slot);
    }
}

void CommandDispatcher::settle(CommandSlot *slot)
{
    slot->more_to_send = m_context.more_to_send;

    if (slot->more_to_send)
    {
        m_sending = slot;
    }
    else if (m_sending == slot)
    {
        m_sending = nullptr;
    }

    if ((m_active != slot) && (m_sending != slot) && !slot->clearing)
    {
        release(slot);
    }
}

void CommandDispatcher::abort_slot(CommandSlot *slot, const char *reason)
{
    if (slot->command == nullptr)
    {
        return;
    }

    trace("commands: %.*s aborted: %s, %u of %u bytes in", (int)slot->command->name.size(), slot->command->name.data(), reason, slot->received, slot->message_length);

    if (m_active == slot)
    {
        m_active = nullptr;
    }

    bool was_sending = (m_sending == slot);

    bool opened_stream = (m_bound == slot) && m_context.stream_open();

    if (m_sending == slot)
    {
        m_sending = nullptr;
    }

    slot->clearing = false;
    slot->more_to_send = false;

    if (was_sending || opened_stream)
    {
        m_context.stream_abandoned();
    }

    if (slot->command->on_abort != nullptr)
    {
        bind(slot);
        slot->command->on_abort(slot->storage, m_context, reason);
    }

    release(slot);
}

void CommandDispatcher::release(CommandSlot *slot)
{
    const Command *command = slot->command;

    slot->command = nullptr;
    slot->clearing = false;

    if (m_bound == slot)
    {
        m_bound = nullptr;
    }

    command->destroy(slot->storage);
}

CommandSlot *CommandDispatcher::take_slot()
{
    for (CommandSlot& slot : m_slots)
    {
        if (slot.command == nullptr)
        {
            return &slot;
        }
    }

    return nullptr;
}

CommandSlot *CommandDispatcher::find_ticket(uint32_t ticket)
{
    for (CommandSlot& slot : m_slots)
    {
        if ((slot.command != nullptr) && (slot.ticket == ticket))
        {
            return &slot;
        }
    }

    return nullptr;
}
