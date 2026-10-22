/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef PICO_NONE_TLS_LISTENER_H
#define PICO_NONE_TLS_LISTENER_H

#include "session.h"

class Listener : private OsPollClient {
public:
    Listener();

    // Ports below 1024 are moved up (80 -> 8080), see os_map_port() in os_linux.h.
    int listen(u16_t port, session_factory_t *factory);
    
private:
    ~Listener();

    void on_poll(short revents) override;

    session_factory_t           *m_session_factory;
    int                          m_fd;
    uint64_t                     m_token;

    static int NUM_LISTENERS;
};

#endif
