/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "core_dump.h"
#include "log_encryption.h"
#include "shared_buffer.h"

class CoreDumpStream
{
public:
    static const uint32_t PIECE = 1024;
    static const uint32_t PACKET = LogEncryption::HEADER_SIZE + 4 + PIECE + LogEncryption::TAG_SIZE;

    static_assert(CoreDump::HEADER_AREA % PIECE == 0, "the dump is whole pieces");

    ~CoreDumpStream() { close(); }

    bool open();
    bool pump(CommandContext& ctx);
    void close();

    bool is_open() const { return m_open; }
    uint32_t remaining() const { return m_remaining; }
    uint32_t offset() const { return m_offset; }

private:
    static const uint32_t PROGRESS_EVERY = 32 * 1024;

    bool seal_next();

    bool m_open = false;
    uint32_t m_remaining = 0;
    uint32_t m_offset = 0;
    uint32_t m_waits = 0;
    uint32_t m_last_space = 0;
    uint32_t m_started_ms = 0;
    SharedBuffer m_packet;
};

struct CoreDumpSend
{
    static constexpr std::string_view name = "coredump";
    static constexpr std::string_view help = "coredump - send the saved core dump, sealed with the log key";

    static constexpr bool streams_output = true;

    bool on_header(CommandContext& ctx);
    bool on_send_space(CommandContext& ctx);
    void on_abort(CommandContext& ctx, const char *reason);

private:
    CoreDumpStream m_stream;
};

struct ListCoreDumps
{
    static constexpr std::string_view name = "list-coredumps";
    static constexpr std::string_view help = "list-coredumps - the saved core dump as json, [] when there is none";
    static constexpr std::string_view ws_prefix = "/dbg/";

    bool on_header(CommandContext& ctx);
};

struct ClearCoreDump
{
    static constexpr std::string_view name = "clear-coredump";
    static constexpr std::string_view help = "clear-coredump - delete the saved core dump, so the next crash saves one";
    static constexpr std::string_view ws_prefix = "/dbg/";

    bool on_header(CommandContext& ctx);
};
