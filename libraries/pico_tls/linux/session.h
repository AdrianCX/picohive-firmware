/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef PICO_SESSION_H
#define PICO_SESSION_H

#include <string>
#include <vector>

#include "os_platform.h"
#include "isession_callback.h"
#include "os_loop.h"

typedef struct mbedtls_ssl_context mbedtls_ssl_context;
typedef struct mbedtls_ssl_config mbedtls_ssl_config;

typedef void (session_factory_t)(void *arg, bool tls);

#ifndef MAX_ACCEPTED_SESSIONS
#define MAX_ACCEPTED_SESSIONS 5
#endif

struct AcceptedSocket
{
    int fd;
    mbedtls_ssl_config *tls_config;
};

class Session
    : public ISessionSender
    , private OsPollClient
{
public:
    static void create_client_tls_config(const uint8_t *cert, size_t cert_len);

    Session(void *arg = NULL, bool tls = false);
    virtual ~Session();

    void set_tls(bool tls) { m_tls = tls; }
    void set_debug(bool debug) { m_debug = debug; }

    bool resolving() const { return m_resolving; }

    void set_keepalive(u32_t idle_ms, u32_t interval_ms, u32_t count)
    {
        m_keepIdleMs = idle_ms;
        m_keepIntervalMs = interval_ms;
        m_keepCount = count;
    }
    
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

    void *get_pcb() { return (m_fd >= 0) ? (void *)this : NULL; }
private:
    enum class Phase
    {
        Idle,
        Connecting,
        Handshake,
        Open,
    };

    void on_poll(short revents) override;

    void on_connect_done();
    void do_handshake();
    void do_read();
    bool deliver(uint8_t *data, size_t len);
    void pump_write();
    void update_events();
    void notify_sent();
    void post_sent();
    void fail_connection(err_t err, int sys_errno);
    void post_failure(err_t err, int sys_errno);
    void resolved(err_t err, ip_addr_t addr);
    void drop_socket();
    void setup_tls(mbedtls_ssl_config *config, bool client);
    void apply_socket_options();

    bool m_connected;
    bool m_closing;
    bool m_processing;
    bool m_tls;
    bool m_debug;
    bool m_accepted;
    u16_t m_port;

    uint64_t m_connect_us = 0;
    int32_t m_resolved_ms = -1;
    bool m_resolving = false;
    int32_t m_connected_ms = -1;
    uint32_t m_acked = 0;
    uint32_t m_received = 0;
    u32_t m_keepIdleMs = 0;
    u32_t m_keepIntervalMs = 0;
    u32_t m_keepCount = 0;
    
    ISessionCallback *m_callback;

    uint64_t m_token;
    int m_fd = -1;
    Phase m_phase = Phase::Idle;
    bool m_want_write = false;
    mbedtls_ssl_context *m_ssl = NULL;
    std::string m_host;

    std::vector<uint8_t> m_out;
    size_t m_out_at = 0;
    size_t m_tls_write_len = 0;
    uint32_t m_unreported = 0;
    bool m_sent_posted = false;
    
    static int NUM_SESSIONS;
    static int NUM_ACCEPTED;
};

#endif
