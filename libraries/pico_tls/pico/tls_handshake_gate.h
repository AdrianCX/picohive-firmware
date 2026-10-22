/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef PICO_TLS_HANDSHAKE_GATE_H
#define PICO_TLS_HANDSHAKE_GATE_H

#include <stdint.h>

#include "pico_tls_common.h"

class Session;

class TLSHandshakeGate
{
public:
    static bool enter(Session *session);

    static void leave(Session *session, const char *why);

private:
    static void start_next(void *arg);
    static void timeout(void *arg);

    static Session *m_active;
    static Session *m_queue[TLS_HANDSHAKE_QUEUE_SIZE];
    static uint32_t m_start;
};

#endif
