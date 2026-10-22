/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef PICO_SESSION_H
#define PICO_SESSION_H

#include "pico/cyw43_arch.h"
#include "lwip/altcp_tcp.h"
#include "isession_callback.h"
#include "tls_handshake_gate.h"

// Values for lwip err_t 
//
//  0,             /* ERR_OK          0      No error, everything OK. */
//  ENOMEM,        /* ERR_MEM        -1      Out of memory error.     */
//  ENOBUFS,       /* ERR_BUF        -2      Buffer error.            */
//  EWOULDBLOCK,   /* ERR_TIMEOUT    -3      Timeout                  */
//  EHOSTUNREACH,  /* ERR_RTE        -4      Routing problem.         */
//  EINPROGRESS,   /* ERR_INPROGRESS -5      Operation in progress    */
//  EINVAL,        /* ERR_VAL        -6      Illegal value.           */
//  EWOULDBLOCK,   /* ERR_WOULDBLOCK -7      Operation would block.   */
//  EADDRINUSE,    /* ERR_USE        -8      Address in use.          */
//  EALREADY,      /* ERR_ALREADY    -9      Already connecting.      */
//  EISCONN,       /* ERR_ISCONN     -10     Conn already established.*/
//  ENOTCONN,      /* ERR_CONN       -11     Not connected.           */
//  -1,            /* ERR_IF         -12     Low-level netif error    */
//  ECONNABORTED,  /* ERR_ABRT       -13     Connection aborted.      */
//  ECONNRESET,    /* ERR_RST        -14     Connection reset.        */
//  ENOTCONN,      /* ERR_CLSD       -15     Connection closed.       */
//  EIO            /* ERR_ARG        -16     Illegal argument.        */

extern altcp_allocator_t tcp_allocator;

typedef void (session_factory_t)(void *arg, bool tls);

#ifndef MAX_ACCEPTED_SESSIONS
#define MAX_ACCEPTED_SESSIONS 5
#endif

// Can be either TLS or regular TCP, abstracted away by altcp.
class Session
    : public ISessionSender
{
public:
    static void create_client_tls_config(const uint8_t *cert, size_t cert_len);

    Session(void *arg = NULL, bool tls = false);
    virtual ~Session();

    void set_tls(bool tls) { m_tls = tls; }
    void set_debug(bool debug) { m_debug = debug; }

    // Cannot cancel DNS lookups so don't delete if this is true
    bool resolving() const { return m_resolving; }

    const char *host() const { return m_host; }

    err_t resume_connect() { return connect(m_host, &m_ip, m_port); }

    // TCP keepalive
    void set_keepalive(u32_t idle_ms, u32_t interval_ms, u32_t count)
    {
        m_keepIdleMs = idle_ms;
        m_keepIntervalMs = interval_ms;
        m_keepCount = count;
    }
    
    // ISender
    virtual err_t connect(const char *host, u16_t port) override;
    virtual err_t connect(const char *host, const ip_addr_t *ipaddr, u16_t port) override;
    virtual err_t send(const u8_t *data, size_t len) override;
    virtual err_t close() override;
    virtual err_t flush() override;
    virtual u16_t send_buffer_size() override;
    virtual bool is_connected() override;

    void set_callback(ISessionCallback *callback) { m_callback = callback; }

    static int get_num_sessions() { return NUM_SESSIONS; }
    static int get_num_accepted() { return NUM_ACCEPTED; }

    void *get_pcb() { return m_pcb; }
private:
    static err_t lwip_connected(void *arg, struct altcp_pcb *pcb, err_t err);
    static err_t lwip_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err);
    static err_t lwip_sent(void *arg, struct altcp_pcb *pcb, u16_t len);
    static err_t lwip_close_or_abort_conn(struct altcp_pcb *pcb, Session *hs, u8_t abort_conn);
    static err_t lwip_close_conn(struct altcp_pcb *pcb, Session *hs);
    static void lwip_err(void *arg, err_t err);
    static void dns_callback(const char* hostname, const ip_addr_t *ipaddr, void *arg);

    void init_pcb();
    err_t check_send_failure(err_t err);

    bool m_connected;
    bool m_closing;
    bool m_processing;
    bool m_tls;
    bool m_debug;
    bool m_accepted;
    u16_t m_sentBytes;
    u16_t m_port;

    uint64_t m_connect_us = 0;
    int32_t m_resolved_ms = -1;     // -1: not yet
    bool m_resolving = false;
    int32_t m_connected_ms = -1;
    uint32_t m_acked = 0;
    uint32_t m_received = 0;
    u32_t m_keepIdleMs = 0;
    u32_t m_keepIntervalMs = 0;
    u32_t m_keepCount = 0;

    const char *m_host = NULL;
    ip_addr_t m_ip = {};
    
    ISessionCallback *m_callback;
    struct altcp_pcb *m_pcb;
    
    static int NUM_SESSIONS;
    static int NUM_ACCEPTED;
};

#endif
