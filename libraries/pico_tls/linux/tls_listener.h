/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef MBEDTLS_TLS_LISTENER_H
#define MBEDTLS_TLS_LISTENER_H

#include "session.h"

class TLSListener : private OsPollClient {
public:
    TLSListener();

    int listen(u16_t port, session_factory_t *factory);
    
private:
    ~TLSListener();

    void on_poll(short revents) override;

    session_factory_t           *m_session_factory;
    mbedtls_ssl_config          *m_conf;
    int                          m_fd;
    uint64_t                     m_token;
    
    static const char *m_alpn_strings[];
};

#endif
