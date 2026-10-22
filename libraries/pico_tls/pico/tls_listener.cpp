/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "pico_tls_common.h"
#include "tls_listener.h"
#include "session.h"
#include "mbedtls_wrapper.h"

#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"

#if defined(MBEDTLS_DEBUG_C)
#include "mbedtls/debug.h"
#endif

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
    , m_bind_pcb(NULL)
    , m_listen_pcb(NULL)
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
    
    if (m_session_factory != NULL)
	{
        trace("TLS Listener is already initialized");
        return -1;
    }
    
#if !TLS_LISTENER_HAS_CERTIFICATE
    trace("TLSListener::listen: this=%p, no certificate compiled in, make one with os/util/make_cert.sh <address>\n", this);
    return -1;
#else
    m_session_factory = factory;

    // Create mbedtls config via altcp, it stores mbedtls_ssl_config as the first item so we can access it by casting.
    m_conf = (struct altcp_tls_config *)altcp_tls_create_config_server_privkey_cert(__key_der, __key_der_len, NULL, 0, __cert_der, __cert_der_len);

    // ALPN for quicker connection establishment
    altcp_tls_configure_alpn_protocols(m_conf, &m_alpn_strings[0]);

#if defined(MBEDTLS_DEBUG_C)
    // useful for debugging TLS problems, prints out whole packets and all logic in mbedtls if define is present.
    mbedtls_ssl_conf_dbg( (mbedtls_ssl_config *)m_conf, mbedtls_debug_print, NULL );
    mbedtls_debug_set_threshold(5);
#endif

    // Bind to the given port on any ip address
    m_bind_pcb = (struct altcp_pcb *)altcp_tls_new(m_conf, IPADDR_TYPE_ANY);
    altcp_setprio(m_bind_pcb, TCP_PRIO_MIN);
    err_t err = altcp_bind(m_bind_pcb, IP_ANY_TYPE, port);

    if (err != ERR_OK)
	{
        trace("Bind error: %s\r\n", lwip_strerr(err));
        return err;
    }

    m_listen_pcb = altcp_listen(m_bind_pcb);
    if (m_listen_pcb == NULL)
	{
        trace("Listen failed\r\n");
        return -1;
    }
    
    altcp_arg(m_listen_pcb, this);
    altcp_accept(m_listen_pcb, http_accept);

    trace("TLSListener::TLSListener: this=%p, port=%d, bind_pcb=%p, listen_pcb=%p\n", this, (int)port, m_bind_pcb, m_listen_pcb);
    return 0;
#endif
}

err_t TLSListener::http_accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
    trace("TLSListener::http_accept: this=%p, pcb=%p, err=%s\n", arg, pcb, lwip_strerr(err));
    if ((err != ERR_OK) || (pcb == NULL))
	{
        return ERR_VAL;
    }

    if (Session::get_num_accepted() >= MAX_ACCEPTED_SESSIONS) 
    {
        trace("TLSListener::http_accept: this=%p, refused, already %d connections\n", arg, Session::get_num_accepted());
        altcp_abort(pcb);
        return ERR_ABRT;
    }

    ((TLSListener *)arg)->m_session_factory(pcb, true);
    return ERR_OK;
}
