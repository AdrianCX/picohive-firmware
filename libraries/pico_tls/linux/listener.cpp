/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"
#include "listener.h"
#include "session.h"
#include "tls_support.h"

#include <poll.h>

int Listener::NUM_LISTENERS = 0;

Listener::Listener()
    : m_session_factory(NULL)
    , m_fd(-1)
    , m_token(OsLoop::acquire_token())
{
    trace("Listener::Listener: this=%p\n", this);

    ++NUM_LISTENERS;
}

Listener::~Listener()
{
    trace("Listener::~Listener: this=%p\n", this);
    
    --NUM_LISTENERS;
}

int Listener::listen(u16_t port, session_factory_t factory)
{
    trace("Listener::listen: this=%p, port: %d, factory: %p\n", this, (int)port, factory);
    
    if (m_session_factory != NULL) {
        trace("TLS Listener is already initialized");
        return -1;
    }
    
    m_fd = listen_socket(port, "Listener");
    if (m_fd < 0)
    {
        return ERR_USE;
    }

    m_session_factory = factory;
    OsLoop::watch(m_token, m_fd, POLLIN, this);

    trace("Listener::Listener: this=%p, port=%d, listening on %d\n", this, (int)port, local_port(m_fd));
    return 0;
}

void Listener::on_poll(short revents)
{
    int fd = 0;
    while ((fd = accept_socket(m_fd, "Listener")) >= 0)
    {
        trace("Listener::http_accept: this=%p, fd=%d\n", this, fd);
        m_session_factory(new AcceptedSocket{fd, NULL}, false);
    }
}
