/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pending_clears.h"

int PendingClears::add(uint16_t message_id, uint32_t waiter, uint64_t now_us)
{
    for (int i = 0; i < MAX_PENDING_CLEARS; ++i)
    {
        PendingClear& entry = entries[i];

        if (!entry.used)
        {
            entry.message_id = message_id;
            entry.waiter = waiter;
            entry.since = now_us;
            entry.used = true;
            return i;
        }
    }

    return -1;
}

void PendingClears::remove(int index)
{
    if ((index >= 0) && (index < MAX_PENDING_CLEARS))
    {
        entries[index].used = false;
    }
}

bool PendingClears::take(uint16_t message_id, uint32_t& waiter)
{
    for (PendingClear& entry : entries)
    {
        if (entry.used && (entry.message_id == message_id))
        {
            waiter = entry.waiter;
            entry.used = false;
            return true;
        }
    }

    return false;
}

int PendingClears::find_expired(uint64_t now_us, uint64_t timeout_us) const
{
    for (int i = 0; i < MAX_PENDING_CLEARS; ++i)
    {
        if (entries[i].used && ((now_us - entries[i].since) > timeout_us))
        {
            return i;
        }
    }

    return -1;
}

bool PendingClears::full() const
{
    for (const PendingClear& entry : entries)
    {
        if (!entry.used)
        {
            return false;
        }
    }

    return true;
}

void PendingClears::clear_all()
{
    for (PendingClear& entry : entries)
    {
        entry.used = false;
    }
}
