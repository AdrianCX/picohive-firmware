/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include "os_platform.h"

class ISessionCallback
{
public:
    virtual ~ISessionCallback() {};
    
    virtual bool on_sent(uint16_t len) { return true; }
    virtual bool on_recv(uint8_t *data, size_t len) { return true; }
    virtual void on_closed() {};
    virtual void on_connected() {};
};

class ISessionSender
{
public:
    virtual ~ISessionSender() {};
    
    virtual int8_t connect(const char *host, uint16_t port) = 0;
    virtual int8_t connect(const char *host, const ip_addr_t *ipaddr, uint16_t port) = 0;
    virtual int8_t send(const uint8_t *data, size_t len) = 0;
    virtual int8_t flush() = 0;
    virtual int8_t close() = 0;
    virtual uint16_t send_buffer_size() = 0;
    virtual bool is_connected() = 0;
};
