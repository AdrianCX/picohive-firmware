/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#define MAX_PENDING_CLEARS 4

struct PendingClear
{
    bool used = false;
    uint16_t message_id = 0;
    uint32_t waiter = 0;
    uint64_t since = 0;
};

class PendingClears
{
public:
    int add(uint16_t message_id, uint32_t waiter, uint64_t now_us);
    void remove(int index);
    bool take(uint16_t message_id, uint32_t& waiter);
    int find_expired(uint64_t now_us, uint64_t timeout_us) const;
    bool full() const;
    void clear_all();

    PendingClear entries[MAX_PENDING_CLEARS];
};
