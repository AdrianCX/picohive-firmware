/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"
#include "upgrade.h"

Upgrade::~Upgrade()
{
    delete m_helper;
}

bool Upgrade::on_header(CommandContext& ctx)
{
    if (UpgradeHelper::isUpgrading())
    {
        return ctx.reply("an upgrade is already in progress");
    }

    if (ctx.message_length == 0)
    {
        return ctx.reply("empty binary");
    }

    m_helper = UpgradeHelper::create();
    m_ok = true;

    trace("upgrade: receiving %u bytes", ctx.message_length);
    return true;
}

bool Upgrade::on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk)
{
    if (!m_ok)
    {
        return true;
    }

    if (!m_helper->storeData(data, len))
    {
        m_ok = false;
        return ctx.reply(m_helper->lastError());
    }

    return true;
}

void Upgrade::on_abort(CommandContext&, const char *reason)
{
    trace("upgrade: abandoned (%s), %u bytes of firmware were in the staging area, the running firmware is untouched", reason, (m_helper != nullptr) ? m_helper->bytesWritten() : 0);
}

void Upgrade::on_retained_cleared(CommandContext& ctx)
{
    if (!m_ok)
    {
        return;
    }

    if (!m_helper->verifySignature())
    {
        trace("upgrade: refused, %s", m_helper->lastError());
        ctx.reply(m_helper->lastError());
        return;
    }

    trace("upgrade: applying build %s", m_helper->buildId());
	
    m_helper->upgrade();

    ctx.reply("upgrade failed, still on the old firmware");
}

MQTT_COMMAND(Upgrade);
WS_COMMAND(Upgrade);
