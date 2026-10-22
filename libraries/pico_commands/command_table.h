/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <concepts>
#include <cstddef>
#include <new>
#include <span>
#include <string_view>

#include "general_config.h"
#include "reply_buffer.h"
#include <type_traits>

#ifndef COMMAND_REPLY_QUEUE_LENGTH
#define COMMAND_REPLY_QUEUE_LENGTH 8
#endif

#ifndef MAX_COMMAND_INSTANCE_BYTES
#define MAX_COMMAND_INSTANCE_BYTES 256
#endif

enum class Payload : uint8_t
{
    None,      // topic only
    Buffered,  // whole body in one call, at most max_bytes
    Streamed,  // body in chunks, never held whole
};

enum class ClearResult : uint8_t
{
    Done,
    Pending,
    Failed,
};

inline constexpr uint32_t NO_WAITER = 0;

class CommandContext
{
public:
    virtual ~CommandContext() = default;

    std::string_view prefix;
    std::string_view name;
    std::string_view args;
    uint32_t message_length = 0;
    bool more_to_send = false;

    void send_more_later() { more_to_send = true; }
    void sending_done() { more_to_send = false; }

    // Silent commands do not send replies
    bool silenced = false;
    bool dropped_send = false;

    bool reply(const char *text)
    {
        if (silenced)
        {
            dropped_send = true;
            return true;
        }

        uint32_t len = (text != nullptr) ? strlen(text) : 0;
        ReplyBuffer copy(*this, len);

        if (copy && (len > 0))
        {
            memcpy(copy.data(), text, len);
            copy.set_size(len);
        }

        return reply(copy);
    }

    [[gnu::format(printf, 3, 4)]] bool reply_fmt(uint32_t size, const char *format, ...)
    {
        if (silenced)
        {
            dropped_send = true;
            return true;
        }

        ReplyBuffer text(*this, size);

        va_list ap;
        va_start(ap, format);
        text.append_vsnprintf(format, ap);
        va_end(ap);

        return reply(text);
    }

    bool reply(const ReplyBuffer& text)
    {
        if (silenced)
        {
            dropped_send = true;
            return true;
        }

        if (!text)
        {
            return false;
        }

        if (can_send_now(text.name(), text.size()))
        {
            return send_reply(text);
        }

        return queue(text);
    }

    bool begin_stream(std::string_view topic, uint32_t total_bytes)
    {
        if (!open_stream(topic, total_bytes))
        {
            return false;
        }

        m_streamRemaining = total_bytes;
        return true;
    }

    bool send_chunk(const uint8_t *data, uint32_t len)
    {
        if (!write_chunk(data, len))
        {
            m_streamRemaining = 0;
            return false;
        }

        m_streamRemaining = (len < m_streamRemaining) ? (m_streamRemaining - len) : 0;
        return true;
    }

    bool stream_open() const { return m_streamRemaining > 0; }

    virtual uint32_t send_space() = 0;

    virtual uint32_t message_cost(std::string_view topic, uint32_t len) const { return topic.size() + len; }

    void defer_sends(bool defer) { m_defer = defer; }
    void set_queue(std::span<ReplyBuffer> waiting) { m_waiting = waiting; }
    bool can_queue() const { return !m_waiting.empty(); }
    bool has_queued() const { return !m_waiting.empty() && m_waiting[m_queueHead]; }
    bool flush_queued();
    void drop_queued();
    void stream_abandoned()
    {
        if (m_streamRemaining > 0)
        {
            m_streamRemaining = 0;
            on_stream_cut();
        }
    }

    virtual void on_payload_too_large(uint32_t size, uint32_t max_bytes)
    {
        reply_fmt(48, "payload too large: %u > %u", size, max_bytes);
    }

    virtual ClearResult clear_retained(uint32_t waiter) { return ClearResult::Done; }

protected:
    virtual void on_stream_cut() {}

    virtual bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) = 0;
    virtual bool open_stream(std::string_view topic, uint32_t total_bytes) = 0;
    virtual bool write_chunk(const uint8_t *data, uint32_t len) = 0;

private:
    bool can_send_now(std::string_view topic, uint32_t len) { return !m_defer && (send_space() >= message_cost(topic, len)); }

    bool send_reply(const ReplyBuffer& text);
    bool queue(const ReplyBuffer& text);

    uint32_t m_streamRemaining = 0;
    bool m_defer = false;
    std::span<ReplyBuffer> m_waiting;
    uint32_t m_queueHead = 0;
    uint32_t m_queueTail = 0;
};

// A command class compiled down to plain data, so the table sits in flash.
struct Command
{
    std::string_view prefix;
    std::string_view name;
    std::string_view help;

    Payload payload = Payload::None;
    bool clears_retained = false;
    bool exclusive = false;
    bool silent = false;
    uint32_t max_bytes = 0;
    uint16_t instance_size = 0;

    void (*construct)(void *storage) = nullptr;
    void (*destroy)(void *storage) = nullptr;

    // All but on_header may be missing; see verify_command_class.
    bool (*on_header)(void *instance, CommandContext&) = nullptr;
    bool (*on_data)(void *instance, CommandContext&, const uint8_t *data, uint32_t len, bool last_chunk) = nullptr;
    bool (*on_send_space)(void *instance, CommandContext&) = nullptr;
    void (*on_abort)(void *instance, CommandContext&, const char *reason) = nullptr;
    void (*on_retained_cleared)(void *instance, CommandContext&) = nullptr;
};

const Command *find_command(std::span<const Command> table, std::string_view topic, std::string_view& args);

template <typename Class>
concept HasName = requires { { Class::name } -> std::convertible_to<std::string_view>; };

template <typename Class>
concept HasHelp = requires { { Class::help } -> std::convertible_to<std::string_view>; };

template <typename Class>
concept HasMaxBytes = requires { { Class::max_bytes } -> std::convertible_to<uint32_t>; };

template <typename Class>
concept HasOnHeader = requires (Class& c, CommandContext& ctx) { { c.on_header(ctx) } -> std::same_as<bool>; };

template <typename Class>
concept HasOnPayload = requires (Class& c, CommandContext& ctx, std::string_view body) { { c.on_payload(ctx, body) } -> std::same_as<bool>; };

template <typename Class>
concept HasOnChunk = requires (Class& c, CommandContext& ctx, const uint8_t *data, uint32_t len, bool last) { { c.on_chunk(ctx, data, len, last) } -> std::same_as<bool>; };

template <typename Class>
concept HasOnAbort = requires (Class& c, CommandContext& ctx, const char *reason) { { c.on_abort(ctx, reason) } -> std::same_as<void>; };

template <typename Class>
concept HasOnSendSpace = requires (Class& c, CommandContext& ctx) { { c.on_send_space(ctx) } -> std::same_as<bool>; };

template <typename Class>
concept HasOnRetainedCleared = requires (Class& c, CommandContext& ctx) { { c.on_retained_cleared(ctx) } -> std::same_as<void>; };

#define COMMAND_TRAIT(FUNCTION, MEMBER, TYPE, DEFAULT)                                   \
    template <typename Class>                                                            \
    consteval TYPE FUNCTION()                                                            \
    {                                                                                    \
        if constexpr (requires { { Class::MEMBER } -> std::convertible_to<TYPE>; })      \
            return Class::MEMBER;                                                        \
        else                                                                             \
            return DEFAULT;                                                              \
    }

COMMAND_TRAIT(name_of, name, std::string_view, std::string_view())
COMMAND_TRAIT(help_of, help, std::string_view, std::string_view())
COMMAND_TRAIT(payload_of, payload, Payload, Payload::None)
COMMAND_TRAIT(max_bytes_of, max_bytes, uint32_t, 0)
COMMAND_TRAIT(streams_output_of, streams_output, bool, false)
COMMAND_TRAIT(clears_retained_of, clears_retained, bool, false)
COMMAND_TRAIT(exclusive_of, exclusive, bool, false)
COMMAND_TRAIT(silent_of, silent, bool, false)

// Every rule a command class has to meet, each with the line to add.
template <typename Class>
consteval bool verify_command_class()
{
    constexpr Payload mode = payload_of<Class>();
    constexpr bool streams_output = streams_output_of<Class>();
    constexpr bool silent = silent_of<Class>();

    static_assert(!silent || !streams_output, "silent means this command never writes to the link, which is the opposite of streams_output");
    static_assert(!silent || !clears_retained_of<Class>(), "clears_retained publishes the delete, so it is not silent");

    static_assert(HasName<Class>, "command needs: static constexpr std::string_view name = \"...\";");
    static_assert(HasHelp<Class>, "command needs: static constexpr std::string_view help = \"...\";");

    if constexpr (mode == Payload::None)
    {
        static_assert(HasOnHeader<Class>,
            "command needs: bool on_header(CommandContext& ctx); (or a payload mode - static constexpr Payload payload = ...)");
        static_assert(!HasOnPayload<Class>, "on_payload needs: static constexpr Payload payload = Payload::Buffered;");
        static_assert(!HasOnChunk<Class>, "on_chunk needs: static constexpr Payload payload = Payload::Streamed;");
        static_assert(streams_output || !HasOnAbort<Class>, "on_abort on a command with no body and no output: nothing to abort");
    }
    else if constexpr (mode == Payload::Buffered)
    {
        static_assert(HasOnPayload<Class>, "buffered command needs: bool on_payload(CommandContext& ctx, std::string_view body);");
        static_assert(HasMaxBytes<Class>, "buffered command needs: static constexpr uint32_t max_bytes = <largest body>;");
        static_assert(!HasMaxBytes<Class> || (max_bytes_of<Class>() > 0),
            "max_bytes must be at least 1; the body is held whole, stream a big one (Payload::Streamed)");
        static_assert(!HasOnChunk<Class>, "on_chunk on a buffered command: the body comes whole to on_payload");
    }
    else
    {
        static_assert(HasOnChunk<Class>, "streamed command needs: bool on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk);");
        static_assert(HasOnAbort<Class>, "streamed command needs: void on_abort(CommandContext& ctx, const char *reason); - a body can stop half way");
        static_assert(!HasOnPayload<Class>, "on_payload on a streamed command: the body comes in pieces to on_chunk");
    }

    if constexpr (streams_output)
    {
        static_assert(HasOnSendSpace<Class>, "command streaming output needs: bool on_send_space(CommandContext& ctx);");
        static_assert(HasOnAbort<Class>, "command streaming output needs: void on_abort(CommandContext& ctx, const char *reason); - the link can drop mid transfer");
    }
    else
    {
        static_assert(!HasOnSendSpace<Class>, "on_send_space needs: static constexpr bool streams_output = true;");
    }

    if constexpr (clears_retained_of<Class>())
    {
        static_assert(mode != Payload::None, "clears_retained needs a body: an empty message is never retained");

        // Any other command fires its clear and is gone before it lands.
        static_assert(!HasOnRetainedCleared<Class> || exclusive_of<Class>(),
            "on_retained_cleared needs: static constexpr bool exclusive = true; - only an exclusive command waits for its clear");
    }
    else
    {
        static_assert(!HasOnRetainedCleared<Class>, "on_retained_cleared needs: static constexpr bool clears_retained = true;");
    }

    return true;
}

template <typename Class>
struct BufferedInstance
{
    static constexpr uint32_t capacity = max_bytes_of<Class>();

    Class command;
    SharedBuffer body;

    bool accumulate(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk)
    {
        if (!body)
        {
            body = SharedBuffer((ctx.message_length < capacity) ? ctx.message_length : capacity);

            if (!body)
            {
                return false;
            }
        }

        uint32_t size = body.size();
        uint32_t bytes = (len < (body.capacity() - size)) ? len : (body.capacity() - size);

        if (bytes > 0)
        {
            memcpy(body.data() + size, data, bytes);
            body.set_size(size + bytes);
        }

        return last_chunk ? command.on_payload(ctx, body.view()) : true;
    }

    void abandon(CommandContext& ctx, const char *reason)
    {
        if constexpr (HasOnAbort<Class>)
        {
            command.on_abort(ctx, reason);
        }
    }
};

// A static_assert message cannot name the class, so the check is a template, the error says "in instantiation of CommandFitsASlot<Prompts, 4104, 256>" the command, its size and the room there is.
template <typename Class, size_t Size, size_t Room>
struct CommandFitsASlot
{
    static_assert(Size <= Room, "command does not fit a slot: keep big buffers out of it (a SharedBuffer or ReplyBuffer while it runs), or raise MAX_COMMAND_INSTANCE_BYTES");
    static constexpr bool value = true;
};

template <typename Class>
struct CommandEntry
{
    static constexpr Payload mode = payload_of<Class>();
    static constexpr uint32_t capacity = max_bytes_of<Class>();

    using Instance = std::conditional_t<mode == Payload::Buffered, BufferedInstance<Class>, Class>;

    static consteval Command make(std::string_view prefix)
    {
        verify_command_class<Class>();

        static_assert(CommandFitsASlot<Class, sizeof(Instance), MAX_COMMAND_INSTANCE_BYTES>::value);
        static_assert(alignof(Instance) <= alignof(std::max_align_t), "command needs more alignment than a slot has");

        return Command{
            .prefix = prefix,
            .name = name_of<Class>(),
            .help = help_of<Class>(),
            .payload = mode,
            .clears_retained = clears_retained_of<Class>(),
            .exclusive = exclusive_of<Class>(),
            .silent = silent_of<Class>(),
            .max_bytes = capacity,
            .instance_size = sizeof(Instance),
            .construct = &construct,
            .destroy = &destroy,
            .on_header = header_handler(),
            .on_data = data_handler(),
            .on_send_space = send_space_handler(),
            .on_abort = abort_handler(),
            .on_retained_cleared = retained_cleared_handler(),
        };
    }

private:
    static void construct(void *storage) { new (storage) Instance(); }
    static void destroy(void *storage) { static_cast<Instance *>(storage)->~Instance(); }

    static Class& command_of(void *instance)
    {
        if constexpr (mode == Payload::Buffered)
        {
            return static_cast<Instance *>(instance)->command;
        }
        else
        {
            return *static_cast<Instance *>(instance);
        }
    }

    static bool header(void *i, CommandContext& ctx) { return command_of(i).on_header(ctx); }
    static bool accumulate(void *i, CommandContext& ctx, const uint8_t *d, uint32_t n, bool last) { return static_cast<Instance *>(i)->accumulate(ctx, d, n, last); }
    static bool chunk(void *i, CommandContext& ctx, const uint8_t *d, uint32_t n, bool last) { return command_of(i).on_chunk(ctx, d, n, last); }
    static bool send_space(void *i, CommandContext& ctx) { return command_of(i).on_send_space(ctx); }
    static void abandon(void *i, CommandContext& ctx, const char *reason) { static_cast<Instance *>(i)->abandon(ctx, reason); }
    static void abort(void *i, CommandContext& ctx, const char *reason) { command_of(i).on_abort(ctx, reason); }
    static void retained_cleared(void *i, CommandContext& ctx) { command_of(i).on_retained_cleared(ctx); }

    static consteval auto header_handler()
    {
        if constexpr (HasOnHeader<Class>) return &header;
        else return nullptr;
    }

    static consteval auto data_handler()
    {
        if constexpr ((mode == Payload::Buffered) && HasOnPayload<Class>) return &accumulate;
        else if constexpr ((mode == Payload::Streamed) && HasOnChunk<Class>) return &chunk;
        else return nullptr;
    }

    static consteval auto send_space_handler()
    {
        if constexpr (HasOnSendSpace<Class>) return &send_space;
        else return nullptr;
    }

    static consteval auto abort_handler()
    {
        if constexpr (mode == Payload::Buffered) return &abandon;
        else if constexpr (HasOnAbort<Class>) return &abort;
        else return nullptr;
    }

    static consteval auto retained_cleared_handler()
    {
        if constexpr (HasOnRetainedCleared<Class>) return &retained_cleared;
        else return nullptr;
    }
};

// A table the linker builds out of every file's entries: Start and Stop
// bracket one section (see commands.ld). Its order is the link order.
template <const Command *Start, const Command *Stop>
class LinkedCommandSet
{
public:
    static constexpr std::span<const Command> all() { return { Start, static_cast<size_t>(Stop - Start) }; }
    static constexpr size_t size() { return Stop - Start; }

    static const Command *find(std::string_view topic, std::string_view& args) { return find_command(all(), topic, args); }
};
