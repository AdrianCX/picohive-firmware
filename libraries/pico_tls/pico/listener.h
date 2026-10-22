/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef PICO_NONE_TLS_LISTENER_H
#define PICO_NONE_TLS_LISTENER_H

#include "pico/cyw43_arch.h"

#include "session.h"

class Listener {
public:
    Listener();

    // Start up a regular Listener that calls 'factory' on each new accepted client.
    int listen(u16_t port, session_factory_t *factory);
    
private:
    // Cleanup not implemented yet for listener, expecting it to live for the whole runtime of the pico
    ~Listener();

    static err_t http_accept(void *arg, struct altcp_pcb *pcb, err_t err);

    session_factory_t           *m_session_factory;

    altcp_pcb                  *m_bind_pcb;
    altcp_pcb                  *m_listen_pcb;

    static int NUM_LISTENERS;
};

#endif