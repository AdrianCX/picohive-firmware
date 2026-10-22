/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_tls_common.h"
#include "tls_listener.h"
#include "session.h"
#include "tls_support.h"

#include <poll.h>

#if __has_include("certificate/key.h") && __has_include("certificate/cert.h")
#include "certificate/key.h"
#include "certificate/cert.h"
#define TLS_LISTENER_HAS_CERTIFICATE 1
#else
#define TLS_LISTENER_HAS_CERTIFICATE 0
#endif

const char *TLSListener::m_alpn_strings[] = {"http/1.1", NULL};

TLSListener::TLSListener()
    : m_session_factory(NULL)
    , m_conf(NULL)
    , m_fd(-1)
    , m_token(OsLoop::acquire_token())
{
    trace("TLSListener::TLSListener: this=%p\n", this);
}

TLSListener::~TLSListener()
{
    trace("TLSListener::~TLSListener: this=%p\n", this);
}

int TLSListener::listen(u16_t port, session_factory_t factory)
{
    trace("TLSListener::listen: this=%p, port: %d, factory: %p\n", this, (int)port, factory);
    
    if (m_session_factory != NULL) {
        trace("TLS Listener is already initialized");
        return -1;
    }
    
#if !TLS_LISTENER_HAS_CERTIFICATE
    trace("TLSListener::listen: this=%p, no certificate compiled in, make one with os/util/make_cert.sh <address>\n", this);
    return -1;
#else
    m_conf = tls_create_server_config(__key_der, __key_der_len, __cert_der, __cert_der_len);
    if (m_conf == NULL)
    {
        return -1;
    }

    // ALPN for quicker connection establishment
    mbedtls_ssl_conf_alpn_protocols(m_conf, &m_alpn_strings[0]);

    m_fd = listen_socket(port, "TLSListener");
    if (m_fd < 0)
    {
        return ERR_USE;
    }

    m_session_factory = factory;
    OsLoop::watch(m_token, m_fd, POLLIN, this);

    trace("TLSListener::TLSListener: this=%p, port=%d, listening on %d\n", this, (int)port, local_port(m_fd));
    return 0;
#endif
}

void TLSListener::on_poll(short revents)
{
    int fd;
    while ((fd = accept_socket(m_fd, "TLSListener")) >= 0)
    {
        trace("TLSListener::http_accept: this=%p, fd=%d\n", this, fd);
        m_session_factory(new AcceptedSocket{fd, m_conf}, true);
    }
}
