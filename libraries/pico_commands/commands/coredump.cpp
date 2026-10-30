/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "os_platform.h"
#include "coredump.h"
#include "pico_logger.h"
#include "upgrade_helper.h"

bool CoreDumpStream::open()
{
    close();

    if (CoreDump::saved() == nullptr)
    {
        return false;
    }

    m_packet = SharedBuffer(PACKET);
    if (!m_packet)
    {
        return false;
    }

    m_open = true;
    m_remaining = (CoreDump::size() / PIECE) * PACKET;
    m_offset = 0;
    m_waits = 0;
    m_last_space = 0;
    m_started_ms = to_ms_since_boot(get_absolute_time());
    return true;
}

bool CoreDumpStream::seal_next()
{
    uint8_t *packet = (uint8_t *)m_packet.data();

    memcpy(&packet[LogEncryption::HEADER_SIZE + 4], CoreDump::data() + m_offset, PIECE);
    uint32_t len = LogEncryption::instance().seal_packet(packet, LogEncryption::VERSION_CORE_DUMP, m_offset, PIECE);
    m_offset += PIECE;

    return len == PACKET;
}

bool CoreDumpStream::pump(CommandContext& ctx)
{
    while (m_remaining > 0)
    {
        uint32_t space = ctx.send_space();
        if (space < PACKET)
        {
            m_waits++;
            m_last_space = space;
            ctx.send_more_later();
            return true;
        }

        if (!seal_next())
        {
            trace("coredump stream: sealing failed with %u bytes left", m_remaining);
            close();
            return false;
        }

        if (!ctx.send_chunk((const uint8_t *)m_packet.data(), PACKET))
        {
            trace("coredump stream: failed with %u bytes left", m_remaining);
            close();
            return false;
        }

        m_remaining -= PACKET;

        if ((m_offset % PROGRESS_EVERY) == 0)
        {
            trace("coredump stream: %u of %u bytes, %u bytes left to send, waited %u times for room (last %u bytes)",
                  m_offset, CoreDump::size(), m_remaining, m_waits, m_last_space);
        }
    }

    trace("coredump stream: done, %u bytes in %u ms, waited %u times for room", m_offset, to_ms_since_boot(get_absolute_time()) - m_started_ms, m_waits);
    close();
    ctx.sending_done();
    return true;
}

void CoreDumpStream::close()
{
    m_open = false;
    m_remaining = 0;
    m_packet = SharedBuffer();
}

bool CoreDumpSend::on_header(CommandContext& ctx)
{
    if (CoreDump::saved() == nullptr)
    {
        trace("coredump: no core dump saved");
        return ctx.reply("no core dump");
    }

    if (!LogEncryption::instance().enabled())
    {
        trace("coredump: no log key, not sending");
        return ctx.reply("no log key: a core dump is only sent encrypted");
    }

    if (!m_stream.open())
    {
        trace("coredump: no memory for a %u byte packet", CoreDumpStream::PACKET);
        return ctx.reply("out of memory");
    }

    trace("coredump: sending %u bytes as %u bytes of packets on recv/coredump/data, link has %u bytes of room",
          CoreDump::size(), m_stream.remaining(), ctx.send_space());

    if (!ctx.begin_stream("coredump/data", m_stream.remaining()))
    {
        trace("coredump: starting the stream failed");
        m_stream.close();
        return false;
    }

    return m_stream.pump(ctx);
}

bool CoreDumpSend::on_send_space(CommandContext& ctx)
{
    return m_stream.pump(ctx);
}

void CoreDumpSend::on_abort(CommandContext&, const char *reason)
{
    if (m_stream.is_open())
    {
        trace("coredump: stopped at %u of %u bytes, %u bytes left to send, %s", m_stream.offset(), CoreDump::size(), m_stream.remaining(), safestr(reason));
        m_stream.close();
    }
}

bool ListCoreDumps::on_header(CommandContext& ctx)
{
    const CoreDump::Header *header = CoreDump::saved();

    if (header == nullptr)
    {
        return ctx.reply("[]");
    }

    return ctx.reply_fmt(192, "[{\"reason\": \"%s\", \"buildid\": \"%.*s\", \"bytes\": %u}]",
                         restart_reason_str((restart_reason_t)header->pico.reason),
                         (int)strnlen(header->pico.build_id, CoreDump::BUILD_ID_LEN), header->pico.build_id,
                         CoreDump::size());
}

bool ClearCoreDump::on_header(CommandContext& ctx)
{
    if (UpgradeHelper::isUpgrading())
    {
        return ctx.reply("not while an upgrade is in progress");
    }

    if (!CoreDump::clear())
    {
        return ctx.reply("clearing the core dump failed");
    }

    return ctx.reply("no core dump");
}

MQTT_COMMAND(CoreDumpSend);
MQTT_COMMAND(ListCoreDumps);
WS_COMMAND(ListCoreDumps);
MQTT_COMMAND(ClearCoreDump);
WS_COMMAND(ClearCoreDump);
