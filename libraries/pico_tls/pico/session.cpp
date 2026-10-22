/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_logger.h"

#include "session.h"
#include "pico_tls_common.h"
#include "root_certificates.h"

#include <string.h>
#include <time.h>

#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "pico/time.h"

#if defined(MBEDTLS_DEBUG_C)
#include "mbedtls_wrapper.h"
#include "mbedtls/debug.h"
#endif

altcp_allocator_t tcp_allocator {altcp_tcp_alloc, nullptr};

static struct altcp_tls_config *TLS_CLIENT_CONFIG = NULL;

int Session::NUM_SESSIONS = 0;
int Session::NUM_ACCEPTED = 0;

Session::Session(void *arg, bool tls)
    : m_connected(arg != NULL)
    , m_closing(false)
    , m_processing(false)
    , m_tls(tls)
    , m_debug(false)
    , m_accepted(arg != NULL)
    , m_sentBytes(0)
    , m_port(0)
    , m_callback(NULL)
    , m_pcb((struct altcp_pcb *)arg)
{
    //trace("Session::Session: this=%p, pcb=%p, tls=%d, num_sessions=%d\n", this, m_pcb, m_tls, NUM_SESSIONS);

    if (m_pcb)
    {
        init_pcb();
    }
    
    ++NUM_SESSIONS;

    if (m_accepted)
    {
        ++NUM_ACCEPTED;
    }
}

void Session::init_pcb()
{
    altcp_setprio(m_pcb, TCP_PRIO_MIN);

    altcp_arg(m_pcb, this);
    altcp_recv(m_pcb, lwip_recv);
    altcp_err(m_pcb, lwip_err);
    altcp_sent(m_pcb, lwip_sent);

    altcp_nagle_disable(m_pcb);

    if (m_keepIdleMs != 0)
    {
        altcp_keepalive_enable(m_pcb, m_keepIdleMs, m_keepIntervalMs, m_keepCount);
    }
}

Session::~Session()
{
    //trace("Session::~Session: this:%p, m_pcb=%p, m_closing=%d, m_tls=%d\n", this, m_pcb, m_closing, m_tls);

    TLSHandshakeGate::leave(this, "deleted");

    if (m_pcb != NULL)
    {
        m_callback = NULL;    
        close();
    }
    
    --NUM_SESSIONS;

    if (m_accepted)
    {
        --NUM_ACCEPTED;
    }
}

void Session::create_client_tls_config(const uint8_t *cert, size_t cert_len)
{
    if (TLS_CLIENT_CONFIG == NULL)
    {
        TLS_CLIENT_CONFIG = altcp_tls_create_config_client(cert, cert_len);

        if (TLS_CLIENT_CONFIG != NULL)
        {
            mbedtls_ssl_config *conf = (mbedtls_ssl_config *)TLS_CLIENT_CONFIG;

#if TLS_VERIFY_SERVERS
            if (cert == NULL)
            {
                mbedtls_ssl_conf_ca_cb(conf, RootCertificates::find, NULL);
            }

            mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_verify(conf, RootCertificates::check, NULL);

            trace("Session::create_client_tls_config: servers verified against %s", (cert == NULL) ? "the built-in roots" : "the CA given");
#else
            mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);

            trace("Session::create_client_tls_config: WARNING: servers are not verified (TLS_VERIFY_SERVERS=0)");
#endif
        }

#if defined(MBEDTLS_SSL_SRV_C)
        if (TLS_CLIENT_CONFIG != NULL)
        {
            mbedtls_ssl_conf_session_cache((mbedtls_ssl_config *)TLS_CLIENT_CONFIG, NULL, NULL, NULL);
        }
#endif

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
        if (TLS_CLIENT_CONFIG != NULL)
        {
#if (MBEDTLS_SSL_IN_CONTENT_LEN >= 4096) && (MBEDTLS_SSL_OUT_CONTENT_LEN >= 4096)
            unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_4096;
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 2048) && (MBEDTLS_SSL_OUT_CONTENT_LEN >= 2048)
            unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_2048;
#else
            unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_1024;
#endif
            int error = mbedtls_ssl_conf_max_frag_len((mbedtls_ssl_config *)TLS_CLIENT_CONFIG, mfl);

            if (error != 0)
            {
                trace("Session::create_client_tls_config: max fragment length %d refused: -0x%x", (int)mfl, -error);
            }
        }
#endif

#if defined(MBEDTLS_DEBUG_C)
        // useful for debugging TLS problems, prints out whole packets and all logic in mbedtls if define is present.
        mbedtls_ssl_conf_dbg( (mbedtls_ssl_config *)TLS_CLIENT_CONFIG, mbedtls_debug_print, NULL );
        mbedtls_debug_set_threshold(5);
#endif
    }
}

u16_t Session::send_buffer_size()
{
    if (m_pcb == NULL)
    {
        return 0;
    }

    u16_t record_room = altcp_sndbuf(m_pcb);

    if (!m_tls || (record_room == 0) || (m_pcb->inner_conn == NULL))
    {
        return record_room;
    }

    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(m_pcb);
    int expansion = mbedtls_ssl_get_record_expansion(ssl);
    int payload = mbedtls_ssl_get_max_out_record_payload(ssl);

    if ((expansion <= 0) || (payload <= 0))
    {
        return record_room;
    }

    uint32_t tcp = altcp_sndbuf(m_pcb->inner_conn);
    uint32_t record = (uint32_t)payload + (uint32_t)expansion;
    uint32_t rest = tcp % record;
    uint32_t room = (tcp / record) * (uint32_t)payload + ((rest > (uint32_t)expansion) ? (rest - (uint32_t)expansion) : 0);

    return (room > record_room) ? (u16_t)((room < 0xFFFF) ? room : 0xFFFF) : record_room;
}

err_t Session::send(const u8_t *data, size_t len)
{
    //if (m_debug)
    //{
    //    trace("Session::send: this=%p, m_pcb=%p, data=%p, len=%d\n", this, m_pcb, data, len);
    //}
    
    if (data == NULL)
    {
        return ERR_VAL;
    }

    if (m_pcb == NULL)
    {
        return ERR_OK;
    }

    // prevent partial write notifications back to caller
    m_processing = true;

    size_t chunk = 0xFFFF;

    if (m_tls)
    {
        int max_payload = mbedtls_ssl_get_max_out_record_payload((mbedtls_ssl_context *)altcp_tls_context(m_pcb));
        chunk = (max_payload > 0) ? (size_t)max_payload : MBEDTLS_SSL_OUT_CONTENT_LEN;
    }

    while (len > 0)
    {
        int bytesToSend = (len > chunk) ? chunk : len;
        
        //if (m_debug)
        //{
        //    trace("Session::send: this=%p, m_pcb=%p, data=%p, len=%d bytesToSend=%d\n", this, m_pcb, data, len, bytesToSend);
        //}

        err_t err = altcp_write(m_pcb, data, bytesToSend, TCP_WRITE_FLAG_COPY);
        if (check_send_failure(err) != ERR_OK)
        {
            return err;
        }
        
        data += bytesToSend;
        len -= bytesToSend;
    }

    err_t err = flush();
    if (check_send_failure(err) != ERR_OK)
    {
        return err;
    }

    m_processing = false;
    
    if (m_sentBytes > 0)
    {
        return lwip_sent(this, m_pcb, 0);
    }

    return ERR_OK;
}

err_t Session::check_send_failure(err_t err)
{
    if (err != ERR_OK)
    {
        trace("Session::check_send_failure: this=%p m_pcb=%p m_closing=%d err=%d return err\n", this, m_pcb, m_closing, err);
        
        m_processing = false;
        return err;
    }
    
    if (m_closing || m_pcb == NULL)
    {
        trace("Session::check_send_failure: this=%p m_pcb=%p m_closing=%d err=%d return ERR_ABRT\n", this, m_pcb, m_closing, err);
        
        m_processing = false;
        if (m_callback)
        {
            m_callback->on_closed();
        }
        return ERR_ABRT;
    }

    return ERR_OK;
}

err_t Session::flush()
{
    //if (m_debug)
    //{
    //    trace("Session::flush: this=%p, m_closing=%d, m_pcb=%p\n", this, m_closing, m_pcb);
    //}
    
    if (m_closing || m_pcb == NULL)
    {
        return ERR_OK;
    }
    
    return altcp_output(m_pcb);
}

err_t Session::lwip_sent(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    if (arg == NULL)
    {
        trace("Session::lwip_sent: ERROR: Invalid lwip_sent with NULL arg\n");
        return ERR_VAL;
    }

    Session *self = ((Session *)arg);

    self->m_acked += len;

    //if (self->m_debug)
    //{
    //    trace("Session::lwip_sent: this=%p, m_pcb=%p, len=%d\n", arg, pcb, len);
    //}
    
    if (self->m_processing)
    {
        self->m_sentBytes += len;
    }
    else
    {
        len += self->m_sentBytes;
        self->m_sentBytes = 0;
        
        if (self->m_callback)
        {
            if (!self->m_callback->on_sent(len))
            {
                return self->close();
            }
        }
    }
    return ERR_OK;
}

err_t Session::lwip_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    Session *self = (Session *)arg;

    //if (self->m_debug)
    //{
    //    trace("Session::lwip_recv: this=%p, pcb=%p, m_pcb=%p, pbuf=%p, data=%p, len=%d, tot_len=%d, ref=%d, next=%p, m_closing=%d err=%s\n", arg, pcb, (self != NULL ? self->m_pcb : NULL), p, (p != NULL ? p->payload : NULL), (p != NULL ? p->len : 0), (p != NULL ? p->tot_len : 0), (p != NULL ? p->ref : 0), (p != NULL ? p->next : 0), self->m_closing, lwip_strerr(err));
    //}

    if (err == ERR_ABRT)
    {
        self->m_pcb = NULL;
    }
    
    // RX side is closed for the connection
    if (p == NULL)
    {
        trace("Session::lwip_recv: this=%p connection is closed by remote party.\n", self);
        return self->close();
    }
    
    if (err != ERR_OK)
    {
        trace("Session::lwip_recv: this=%p err[%d] [%s]\n", self, err, lwip_strerr(err));
        self->close();
        return ERR_ABRT;
    }

    self->m_received += p->tot_len;

    if (self->m_callback)
    {
        self->m_processing = true;
        for (struct pbuf *it = p; it != NULL; it = it->next)
        {
            bool recvStatus = self->m_callback->on_recv((u8_t *)it->payload, it->len);

            if (self->m_closing || self->m_pcb == NULL)
            {
                trace("Session::lwip_recv: this=%p closed while handling %d bytes", self, it->len);

                self->m_processing = false;
                if (self->m_callback)
                {
                    self->m_callback->on_closed();
                }
                return ERR_ABRT;
            }
            
            if (!recvStatus)
            {
                trace("Session::lwip_recv: this=%p closing, the handler refused %d bytes", self, it->len);

                self->m_processing = false;
                
                altcp_recved(pcb, p->tot_len);
                pbuf_free(p);
                
                return self->close();
            }

            if (it->tot_len == it->len)
            {
                break;
            }
        }
        self->m_processing = false;
    }

    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void Session::lwip_err(void *arg, err_t err)
{
    trace("Session::lwip_err: this=%p, err=%d, err_str=%s\n", arg, err, lwip_strerr(err));

    Session *self = (Session *)arg;
    if (self == NULL)
    {
        trace("Session::lwip_err: ERROR: Invalid lwip_err with NULL arg\n");
        return;
    }

    TLSHandshakeGate::leave(self, "connection failed");

    if (self->m_connect_us != 0)
    {
        uint32_t since_ms = (uint32_t)((time_us_64() - self->m_connect_us) / 1000);

        const char *meaning = (err == ERR_RST)  ? "reset by the peer"
                            : (err == ERR_CLSD) ? (self->m_tls ? "tls handshake failed" : "closed")
                            : ((err == ERR_ABRT) && self->m_connected && (self->m_received == 0)) ? "the peer closed it without answering"
                            : (err == ERR_ABRT) ? "aborted"
                            : (err == ERR_TIMEOUT) ? "timed out"
                            : "failed";

        trace("Session::lwip_err: this=%p %s, %u ms after connect: address after %d ms, connected after %d ms (-1: never), "
              "%u bytes sent and acked, %u received",
              arg, meaning, (unsigned int)since_ms, (int)self->m_resolved_ms, (int)self->m_connected_ms,
              (unsigned int)self->m_acked, (unsigned int)self->m_received);
    }

    altcp_arg(self->m_pcb, NULL);
    altcp_recv(self->m_pcb, NULL);
    altcp_err(self->m_pcb, NULL);
    altcp_sent(self->m_pcb, NULL);
    self->m_pcb = NULL;
    
    self->close();
}

err_t Session::close()
{
    //if (m_debug)
    //{
    //    trace("Session::close: this=%p, m_pcb=%p, m_closing=%d\n", this, (void *)m_pcb, m_closing);
    //}
    
    TLSHandshakeGate::leave(this, "closed");

    if (m_closing)
    {
        return ERR_OK;
    }

    m_connected = false;
    m_closing = true;

    if (m_pcb == NULL)
    {
        if (!m_processing && m_callback)
        {
            m_callback->on_closed();
        }
        return ERR_OK;
    }

    altcp_arg(m_pcb, NULL);
    altcp_recv(m_pcb, NULL);
    altcp_err(m_pcb, NULL);
    altcp_sent(m_pcb, NULL);

    altcp_abort(m_pcb);
    
    m_pcb = NULL;

    if (!m_processing && m_callback)
    {
        m_callback->on_closed();
    }
    return ERR_ABRT;
}


err_t Session::connect(const char *host, u16_t port)
{
    //if (m_debug)
    //{
    //    trace("Session::connect: this=%p host=%s, port=%d\n", this, safestr(host), port);
    //}

    m_closing = false;
    m_connected = false;
    m_sentBytes = 0;
    
    m_port = port;
    m_host = host;

    m_connect_us = time_us_64();
    m_resolved_ms = -1;
    m_connected_ms = -1;
    m_acked = 0;
    m_received = 0;
    
    ip_addr_t target_ip;
    err_t err = dns_gethostbyname(host, &target_ip, dns_callback, this);

    if (err == ERR_OK)
    {
        m_resolved_ms = 0;
        return connect(host, &target_ip, port);
    }
    else if (err == ERR_INPROGRESS)
    {
        m_resolving = true;
    }
    else
    {
        trace("Session::connect: this=%p host=%s, port=%d, err=%d\n", this, safestr(host), port, err);
    }

    return err;
}

void Session::dns_callback(const char* host, const ip_addr_t *ipaddr, void *arg)
{
    Session *self = (Session *)arg;
    self->m_resolving = false;

    // Closed while the lookup ran, the owner already had on_closed(), connecting now would start, a connection (and a tls handshake) nobody waits for.
    if (self->m_closing)
    {
        trace("Session::dns_callback: this=%p host=%s answered after the session was closed, not connecting", arg, safestr(host));
        return;
    }

    //if (self->m_debug)
    //{
    //    trace("Session::dns_callback: this=%p host=%s, ip=%p\n", arg, safestr(host), ipaddr);
    //}
    
    if (!ipaddr)
    {
        if (self->m_callback)
        {
            self->m_callback->on_closed();
        }

        trace("Session::dns_callback: this=%p host=%s, ip=%p, error looking up address. \n", arg, safestr(host), ipaddr);
        return;
    }

    self->m_resolved_ms = (int32_t)((time_us_64() - self->m_connect_us) / 1000);
    trace("Session::dns_callback: this=%p host=%s is %s, after %d ms", arg, safestr(host), ipaddr_ntoa(ipaddr), (int)self->m_resolved_ms);

    self->connect(self->m_host, ipaddr, self->m_port);
}

err_t Session::connect(const char *host, const ip_addr_t *ipaddr, u16_t port)
{
    //if (m_debug)
    //{
    //    trace("Session::connect: this=%p host=%s, ip=%p, port=%d\n", this, safestr(host), ipaddr ? ipaddr->addr : 0, port);
    //}

    if (!m_tls)
    {
        m_pcb = (struct altcp_pcb *)altcp_new_ip_type(&tcp_allocator, IPADDR_TYPE_ANY);
    }
    else
    {
        if (TLS_CLIENT_CONFIG == NULL)
        {
            trace("Session::connect: this=%p host=%s, ip=%p, port=%d, tls client config was not created.\n", this, safestr(host), ipaddr ? ipaddr->addr : 0, port);
            close();
            return ERR_ARG;
        }

        m_host = host;
        m_port = port;

        if ((ipaddr != NULL) && (ipaddr != &m_ip))
        {
            ip_addr_copy(m_ip, *ipaddr);
        }

        if (!TLSHandshakeGate::enter(this))
        {
            return ERR_OK;
        }

        m_pcb = (struct altcp_pcb *)altcp_tls_new(TLS_CLIENT_CONFIG, IPADDR_TYPE_ANY);

        if ((m_pcb != NULL) && (host != NULL))
        {
            mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(m_pcb), host);
        }
    }

    if (m_pcb == NULL)
    {
        trace("Session::connect: this=%p host=%s, port=%d, no memory for the connection\n", this, safestr(host), port);
        close();
        return ERR_MEM;
    }

    init_pcb();
    
    err_t err =  altcp_connect(m_pcb, ipaddr, port, lwip_connected);

    if (err != ERR_OK)
    {
        trace("Session::connect: this=%p host=%s, ip=%p, port=%d, err=%d, err_str=%s\n", this, safestr(host), ipaddr ? ipaddr->addr : 0, port, err, lwip_strerr(err));
        return close();
    }

    return ERR_OK;
}

err_t Session::lwip_connected(void *arg, struct altcp_pcb *pcb, err_t err)
{
    Session *self = (Session *)arg;
    //if (self->m_debug)
    //{
    //    trace("Session::lwip_connected: this=%p, m_pcb=%p, err=%d, callback=%p\n", arg, pcb, err, self->m_callback);
    //}
    
    if (err != ERR_OK)
    {
        return self->close();
    }

    TLSHandshakeGate::leave(self, "handshake done");

    self->m_connected = true;
    self->m_connected_ms = (int32_t)((time_us_64() - self->m_connect_us) / 1000);

    trace("Session::lwip_connected: this=%p %s after %d ms", arg, self->m_tls ? "connected and tls handshake done" : "connected",
          (int)self->m_connected_ms);

    if (self->m_callback)
    {
        self->m_callback->on_connected();
    }
    
    return ERR_OK;
}

bool Session::is_connected()
{
    return m_pcb != NULL && !m_closing && m_connected;
}

