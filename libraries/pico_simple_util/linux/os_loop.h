/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>
#include <functional>

#include "os_platform.h"

class OsPollClient
{
public:
    virtual ~OsPollClient() {}
    virtual void on_poll(short revents) = 0;
};

class OsLoop
{
public:
    static uint64_t acquire_token();
    static void release_token(uint64_t token);
    static bool alive(uint64_t token);

    static void watch(uint64_t token, int fd, short events, OsPollClient *client);
    static void unwatch(int fd);

    static void post(uint64_t token, std::function<void()> fn);
    static void resolve(uint64_t token, const char *host, std::function<void(err_t err, ip_addr_t addr)> done);

    static void run_once(int timeout_ms);
};
