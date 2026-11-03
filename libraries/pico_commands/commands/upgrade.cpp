/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"
#include "upgrade.h"
#include "common_functions.h"

Upgrade::~Upgrade()
{
    if (m_started && !m_applying)
    {
        UpgradeHelper::get().finish();
    }
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

    UpgradeHelper::get().start();
    m_started = true;
    m_ok = true;

    trace("upgrade: receiving %u bytes", (unsigned int)ctx.message_length);
    return true;
}

bool Upgrade::on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk)
{
    if (!m_ok)
    {
        return true;
    }

    if (!UpgradeHelper::get().storeData(data, len))
    {
        m_ok = false;
        return ctx.reply(UpgradeHelper::get().lastError());
    }

    return true;
}

void Upgrade::on_abort(CommandContext&, const char *reason)
{
    trace("upgrade: abandoned (%s), %u bytes of firmware were in the staging area, the running firmware is untouched", reason, (unsigned int)(m_started ? UpgradeHelper::get().bytesWritten() : 0));
}

void Upgrade::on_retained_cleared(CommandContext& ctx)
{
    if (!m_ok)
    {
        return;
    }

    UpgradeHelper& helper = UpgradeHelper::get();

    if (!helper.verifySignature())
    {
        trace("upgrade: refused, %s", helper.lastError());
        ctx.reply(helper.lastError());
        return;
    }

    m_applying = true;
    defer_upgrade();

    trace("upgrade: build %s verified", helper.buildId());
    ctx.reply(restart_allowed() ? "applying the upgrade" : "applying the upgrade once the app is idle");
}

MQTT_COMMAND(Upgrade);
WS_COMMAND(Upgrade);
