/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "common_functions.h"
#include "upgrade_helper.h"

static bool restart_is_pending = false;
static restart_reason_t pending_reason = RESTART_UNKNOWN;
static uint64_t pending_since_us = 0;

void request_restart(restart_reason_t reason)
{
    switch (reason)
    {
        case RESTART_REBOOT_COMMAND:
        case RESTART_UPGRADE:
        case RESTART_WIFI_LINK_LOST:
            break;

        default:
            force_restart(reason);
            return;
    }

    if (restart_is_pending)
    {
        if ((reason == RESTART_UPGRADE) && (pending_reason != RESTART_UPGRADE))
        {
            pending_reason = reason;
        }
        return;
    }

    restart_is_pending = true;
    pending_reason = reason;
    pending_since_us = to_us_since_boot(get_absolute_time());

    if (!restart_allowed())
    {
        trace("request_restart: %s waits for the app to be idle, %u seconds at most", restart_reason_str(reason), (unsigned int)(RESTART_MAX_WAIT_US / 1000000));
    }
}

void defer_upgrade()
{
    request_restart(RESTART_UPGRADE);
}

bool restart_pending()
{
    return restart_is_pending;
}

bool restart_allowed()
{
    return (app_info().can_restart == nullptr) || app_info().can_restart();
}

void update_restart(uint64_t now_us)
{
    if (!restart_is_pending)
    {
        return;
    }

    bool allowed = restart_allowed();

    if (!allowed && (now_us - pending_since_us < RESTART_MAX_WAIT_US))
    {
        return;
    }

    if (!allowed)
    {
        trace("update_restart: the app is still busy after %u seconds, going ahead", (unsigned int)(RESTART_MAX_WAIT_US / 1000000));
    }

    restart_is_pending = false;

    if (pending_reason != RESTART_UPGRADE)
    {
        trace("update_restart: restarting, %s", restart_reason_str(pending_reason));
        force_restart(pending_reason);
        return;
    }

    trace("update_restart: applying build %s", UpgradeHelper::get().buildId());
    UpgradeHelper::get().upgrade();

    trace("update_restart: upgrade failed, still on the old firmware");
    UpgradeHelper::get().finish();
}
