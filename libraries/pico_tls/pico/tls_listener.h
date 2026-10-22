/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef MBEDTLS_TLS_LISTENER_H
#define MBEDTLS_TLS_LISTENER_H

#include "pico/cyw43_arch.h"

#include "session.h"

struct altcp_tls_config;

class TLSListener {
public:
    TLSListener();

    // Start up a TLS Listener that calls 'factory' on each new accepted client.
    // Certificate is provided via 'certificate/key.h' and 'certificate/cert.h' in include path.
    int listen(u16_t port, session_factory_t *factory);
    
private:
    // Cleanup not implemented yet for listener, expecting it to live for the whole runtime of the pico
    ~TLSListener();

    static err_t http_accept(void *arg, struct altcp_pcb *pcb, err_t err);

    session_factory_t           *m_session_factory;

    altcp_tls_config           *m_conf;
    altcp_pcb                  *m_bind_pcb;
    altcp_pcb                  *m_listen_pcb;
    
    static const char *m_alpn_strings[];
};

#endif