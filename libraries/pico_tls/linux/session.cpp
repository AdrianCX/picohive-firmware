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
#include "tls_support.h"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#pragma push_macro("TCP_MSS")
#include <netinet/tcp.h>
#pragma pop_macro("TCP_MSS")
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

static mbedtls_ssl_config *TLS_CLIENT_CONFIG = NULL;

int Session::NUM_SESSIONS = 0;
int Session::NUM_ACCEPTED = 0;

Session::Session(void *arg, bool tls)
    : m_connected(arg != NULL)
    , m_closing(false)
    , m_processing(false)
    , m_tls(tls)
    , m_debug(false)
    , m_accepted(arg != NULL)
    , m_port(0)
    , m_callback(NULL)
    , m_token(OsLoop::acquire_token())
{
    if (arg != NULL)
    {
        AcceptedSocket *accepted = (AcceptedSocket *)arg;

        m_fd = accepted->fd;
        apply_socket_options();

        if (m_tls)
        {
            setup_tls(accepted->tls_config, false);
            m_phase = Phase::Handshake;
        }
        else
        {
            m_phase = Phase::Open;
        }

        delete accepted;
        update_events();
    }

    ++NUM_SESSIONS;

    if (m_accepted)
    {
        ++NUM_ACCEPTED;
    }
}

Session::~Session()
{
    if ((m_fd >= 0) || (m_ssl != NULL))
    {
        m_callback = NULL;
        close();
        drop_socket();
    }

    OsLoop::release_token(m_token);

    --NUM_SESSIONS;

    if (m_accepted)
    {
        --NUM_ACCEPTED;
    }
}

void Session::create_client_tls_config(const uint8_t *cert, size_t cert_len)
{
    if (TLS_CLIENT_CONFIG != NULL)
    {
        return;
    }

    TLS_CLIENT_CONFIG = tls_create_client_config(cert, cert_len);

    if (TLS_CLIENT_CONFIG == NULL)
    {
        return;
    }

    mbedtls_ssl_config *conf = TLS_CLIENT_CONFIG;

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

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
#if (MBEDTLS_SSL_IN_CONTENT_LEN >= 4096) && (MBEDTLS_SSL_OUT_CONTENT_LEN >= 4096)
    unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_4096;
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 2048) && (MBEDTLS_SSL_OUT_CONTENT_LEN >= 2048)
    unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_2048;
#else
    unsigned char mfl = MBEDTLS_SSL_MAX_FRAG_LEN_1024;
#endif
    int error = mbedtls_ssl_conf_max_frag_len(conf, mfl);

    if (error != 0)
    {
        trace("Session::create_client_tls_config: max fragment length %d refused: -0x%x", (int)mfl, -error);
    }
#endif
}

void Session::apply_socket_options()
{
    int one = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (m_keepIdleMs != 0)
    {
        int idle = std::max<int>(1, m_keepIdleMs / 1000);
        int interval = std::max<int>(1, m_keepIntervalMs / 1000);
        int count = std::max<int>(1, m_keepCount);

        setsockopt(m_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(m_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
    }
}

void Session::setup_tls(mbedtls_ssl_config *config, bool client)
{
    m_ssl = new mbedtls_ssl_context;
    mbedtls_ssl_init(m_ssl);

    int error = mbedtls_ssl_setup(m_ssl, config);
    if (error != 0)
    {
        trace("Session::setup_tls: this=%p, mbedtls_ssl_setup failed: %s", this, tls_error(error));
    }

    if (client && !m_host.empty())
    {
        mbedtls_ssl_set_hostname(m_ssl, m_host.c_str());
    }

    mbedtls_ssl_set_bio(m_ssl, (void *)(intptr_t)m_fd, tls_send, tls_recv, NULL);
}

void Session::drop_socket()
{
    if (m_ssl != NULL)
    {
        mbedtls_ssl_free(m_ssl);
        delete m_ssl;
        m_ssl = NULL;
    }

    if (m_fd >= 0)
    {
        OsLoop::unwatch(m_fd);

        uint8_t discard[1024];
        while (recv(m_fd, discard, sizeof(discard), MSG_DONTWAIT) > 0)
        {
        }

        ::close(m_fd);
        m_fd = -1;
    }

    m_phase = Phase::Idle;
    m_want_write = false;
    m_out.clear();
    m_out_at = 0;
    m_tls_write_len = 0;
    m_unreported = 0;
}

u16_t Session::send_buffer_size()
{
    if (m_fd < 0)
    {
        return 0;
    }

    size_t queued = m_out.size() - m_out_at;
    return (queued >= TCP_SND_BUF) ? 0 : (u16_t)(TCP_SND_BUF - queued);
}

err_t Session::send(const u8_t *data, size_t len)
{
    if (data == NULL)
    {
        return ERR_VAL;
    }

    if (m_fd < 0)
    {
        return ERR_OK;
    }

    if (m_closing)
    {
        trace("Session::check_send_failure: this=%p m_closing=%d return ERR_ABRT\n", this, m_closing);
        return ERR_ABRT;
    }

    // altcp_write() refuses what does not fit the send buffer.
    if (len > send_buffer_size())
    {
        trace("Session::check_send_failure: this=%p, %u bytes do not fit the %u left in the send buffer, err=%d\n", this, (unsigned int)len, (unsigned int)send_buffer_size(), ERR_MEM);
        return ERR_MEM;
    }

    m_out.insert(m_out.end(), data, data + len);

    if (m_phase == Phase::Open)
    {
        pump_write();
    }

    return ERR_OK;
}

err_t Session::flush()
{
    return ERR_OK;
}

void Session::pump_write()
{
    while (m_out_at < m_out.size())
    {
        size_t available = m_out.size() - m_out_at;

        if (m_ssl != NULL)
        {
            size_t len = (m_tls_write_len != 0) ? m_tls_write_len : std::min<size_t>(available, MBEDTLS_SSL_OUT_CONTENT_LEN);
            int written = mbedtls_ssl_write(m_ssl, &m_out[m_out_at], len);

            if (written > 0)
            {
                m_out_at += written;
                m_unreported += written;
                m_tls_write_len = 0;
            }
            else if ((written == MBEDTLS_ERR_SSL_WANT_WRITE) || (written == MBEDTLS_ERR_SSL_WANT_READ))
            {
                m_tls_write_len = len;
                break;
            }
            else
            {
                trace("Session::send: this=%p, tls write failed: %s", this, tls_error(written));
                post_failure((written == MBEDTLS_ERR_NET_CONN_RESET) ? ERR_RST : ERR_ABRT, 0);
                return;
            }
        }
        else
        {
            ssize_t written = ::send(m_fd, &m_out[m_out_at], available, MSG_NOSIGNAL);

            if (written > 0)
            {
                m_out_at += written;
                m_unreported += written;
            }
            else if ((written < 0) && (errno == EINTR))
            {
                continue;
            }
            else if ((written < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
            {
                break;
            }
            else
            {
                post_failure(((errno == ECONNRESET) || (errno == EPIPE)) ? ERR_RST : ERR_ABRT, errno);
                return;
            }
        }
    }

    if (m_out_at == m_out.size())
    {
        m_out.clear();
        m_out_at = 0;
    }
    else if (m_out_at >= 65536)
    {
        m_out.erase(m_out.begin(), m_out.begin() + m_out_at);
        m_out_at = 0;
    }

    update_events();
    post_sent();
}

void Session::update_events()
{
    if (m_fd < 0)
    {
        return;
    }

    short events = 0;
    bool pending = (m_out_at < m_out.size()) || (m_tls_write_len != 0);

    switch (m_phase)
    {
        case Phase::Connecting: events = POLLOUT; break;
        case Phase::Handshake:  events = POLLIN | (m_want_write ? POLLOUT : 0); break;
        case Phase::Open:       events = POLLIN | (pending ? POLLOUT : 0); break;
        case Phase::Idle:       events = 0; break;
    }

    OsLoop::watch(m_token, m_fd, events, this);
}

void Session::post_sent()
{
    if ((m_unreported == 0) || m_sent_posted)
    {
        return;
    }

    m_sent_posted = true;
    OsLoop::post(m_token, [this]() {
        m_sent_posted = false;
        notify_sent();
    });
}

void Session::notify_sent()
{
    uint64_t token = m_token;

    while ((m_unreported > 0) && (m_fd >= 0) && !m_closing)
    {
        u16_t len = (u16_t)std::min<uint32_t>(m_unreported, 0xffff);
        m_unreported -= len;
        m_acked += len;

        if (m_callback && !m_callback->on_sent(len))
        {
            close();
            return;
        }

        if (!OsLoop::alive(token))
        {
            return;
        }
    }
}

void Session::post_failure(err_t err, int sys_errno)
{
    OsLoop::post(m_token, [this, err, sys_errno]() {
        if (!m_closing && (m_fd >= 0))
        {
            fail_connection(err, sys_errno);
        }
    });
}

void Session::fail_connection(err_t err, int sys_errno)
{
    trace("Session::lwip_err: this=%p, err=%d, err_str=%s%s%s\n", this, err, lwip_strerr(err),
          (sys_errno != 0) ? ", " : "", (sys_errno != 0) ? strerror(sys_errno) : "");

    if (m_connect_us != 0)
    {
        uint32_t since_ms = (uint32_t)((time_us_64() - m_connect_us) / 1000);

        const char *meaning = (err == ERR_RST)  ? "reset by the peer"
                            : (err == ERR_CLSD) ? (m_tls ? "tls handshake failed" : "closed")
                            : ((err == ERR_ABRT) && m_connected && (m_received == 0)) ? "the peer closed it without answering"
                            : (err == ERR_ABRT) ? "aborted"
                            : (err == ERR_TIMEOUT) ? "timed out"
                            : "failed";

        trace("Session::lwip_err: this=%p %s, %u ms after connect: address after %d ms, connected after %d ms (-1: never), "
              "%u bytes sent and acked, %u received",
              this, meaning, (unsigned int)since_ms, (int)m_resolved_ms, (int)m_connected_ms,
              (unsigned int)m_acked, (unsigned int)m_received);
    }

    drop_socket();
    close();
}

err_t Session::close()
{
    if (m_closing)
    {
        return ERR_OK;
    }

    if (!m_accepted && (m_connect_us != 0) && ((m_phase == Phase::Connecting) || (m_phase == Phase::Handshake)))
    {
        trace("Session::close: this=%p %s closed by this side %s, %u ms after connect, %u bytes received", this, m_host.c_str(),
              (m_phase == Phase::Connecting) ? "before the tcp connection opened (no answer to the connect)" : "during the tls handshake",
              (unsigned int)((time_us_64() - m_connect_us) / 1000), (unsigned int)m_received);
    }

    m_connected = false;
    m_closing = true;

    if ((m_fd < 0) && (m_ssl == NULL))
    {
        if (!m_processing && m_callback)
        {
            m_callback->on_closed();
        }
        return ERR_OK;
    }

    drop_socket();

    if (!m_processing && m_callback)
    {
        m_callback->on_closed();
    }
    return ERR_ABRT;
}

err_t Session::connect(const char *host, u16_t port)
{
    m_closing = false;
    m_connected = false;

    m_port = port;

    m_connect_us = time_us_64();
    m_resolved_ms = -1;
    m_connected_ms = -1;
    m_acked = 0;
    m_received = 0;

    if (host == NULL)
    {
        trace("Session::connect: this=%p host=null, port=%d\n", this, port);
        return ERR_ARG;
    }

    m_host = host;

    struct in_addr literal;
    if (inet_pton(AF_INET, host, &literal) == 1)
    {
        ip_addr_t target_ip = literal.s_addr;
        m_resolved_ms = 0;
        return connect(host, &target_ip, port);
    }

    m_resolving = true;
    OsLoop::resolve(m_token, host, [this](err_t err, ip_addr_t addr) { resolved(err, addr); });

    return ERR_INPROGRESS;
}

void Session::resolved(err_t err, ip_addr_t addr)
{
    m_resolving = false;

    if (m_closing)
    {
        return;
    }

    if (err != ERR_OK)
    {
        trace("Session::dns_callback: this=%p host=%s, error looking up address. \n", this, m_host.c_str());

        if (m_callback)
        {
            m_callback->on_closed();
        }
        return;
    }

    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr, text, sizeof(text));

    m_resolved_ms = (int32_t)((time_us_64() - m_connect_us) / 1000);
    trace("Session::dns_callback: this=%p host=%s is %s, after %d ms", this, m_host.c_str(), text, (int)m_resolved_ms);

    connect(m_host.c_str(), &addr, m_port);
}

err_t Session::connect(const char *host, const ip_addr_t *ipaddr, u16_t port)
{
    if ((host != NULL) && (host != m_host.c_str()))
    {
        m_host = host;
    }

    m_port = port;

    if (m_fd >= 0)
    {
        drop_socket();
    }

    if (ipaddr == NULL)
    {
        trace("Session::connect: this=%p host=%s, port=%d, no address\n", this, m_host.c_str(), port);
        close();
        return ERR_ARG;
    }

    if (m_tls && (TLS_CLIENT_CONFIG == NULL))
    {
        trace("Session::connect: this=%p host=%s, ip=%p, port=%d, tls client config was not created.\n", this, m_host.c_str(), (void *)(uintptr_t)*ipaddr, port);
        close();
        return ERR_ARG;
    }

    m_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (m_fd < 0)
    {
        trace("Session::connect: this=%p host=%s, port=%d, no socket: %s\n", this, m_host.c_str(), port, strerror(errno));
        close();
        return ERR_MEM;
    }

    apply_socket_options();

    if (m_tls)
    {
        setup_tls(TLS_CLIENT_CONFIG, true);
    }

    m_phase = Phase::Connecting;

    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = *ipaddr;

    if ((::connect(m_fd, (struct sockaddr *)&address, sizeof(address)) != 0) && (errno != EINPROGRESS))
    {
        // lwIP reports this from its error callback, after connect() returned.
        trace("Session::connect: this=%p host=%s, port=%d, err=%s\n", this, m_host.c_str(), port, strerror(errno));
        post_failure((errno == ECONNREFUSED) ? ERR_RST : ERR_RTE, errno);
    }

    update_events();
    return ERR_OK;
}

void Session::on_poll(short revents)
{
    uint64_t token = m_token;

    switch (m_phase)
    {
        case Phase::Connecting:
        {
            on_connect_done();
            return;
        }
        case Phase::Handshake:
        {
            do_handshake();
            return;
        }
        case Phase::Open:
        {
            if (revents & POLLOUT)
            {
                pump_write();
            }

            if (OsLoop::alive(token) && (m_fd >= 0) && (revents & (POLLIN | POLLHUP | POLLERR)))
            {
                do_read();
            }
            return;
        }
        case Phase::Idle:
        {
            return;
        }
    }
}

void Session::on_connect_done()
{
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &len);

    if (error != 0)
    {
        fail_connection((error == ECONNREFUSED) ? ERR_RST : (error == ETIMEDOUT) ? ERR_TIMEOUT : ERR_ABRT, error);
        return;
    }

    if (m_tls)
    {
        trace("Session::lwip_connected: this=%p %s tcp connected after %d ms, tls handshake starting", this, m_accepted ? "incoming" : m_host.c_str(), (int)((time_us_64() - m_connect_us) / 1000));
        m_phase = Phase::Handshake;
        do_handshake();
        return;
    }

    m_phase = Phase::Open;
    m_connected = true;
    m_connected_ms = (int32_t)((time_us_64() - m_connect_us) / 1000);

    trace("Session::lwip_connected: this=%p connected after %d ms", this, (int)m_connected_ms);

    pump_write();

    if (m_callback)
    {
        m_callback->on_connected();
    }
}

void Session::do_handshake()
{
    uint64_t token = m_token;
    int result = mbedtls_ssl_handshake(m_ssl);

    if ((result == MBEDTLS_ERR_SSL_WANT_READ) || (result == MBEDTLS_ERR_SSL_WANT_WRITE))
    {
        m_want_write = (result == MBEDTLS_ERR_SSL_WANT_WRITE);
        update_events();
        return;
    }

    if (result != 0)
    {
        trace("Session::do_handshake: this=%p, %s tls handshake failed: %s", this, m_accepted ? "incoming" : m_host.c_str(), tls_error(result));
        fail_connection(ERR_CLSD, 0);
        return;
    }

    m_phase = Phase::Open;
    m_want_write = false;

    if (!m_accepted)
    {
        m_connected = true;
        m_connected_ms = (int32_t)((time_us_64() - m_connect_us) / 1000);

#if defined(MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH)
        trace("Session::lwip_connected: this=%p %s connected and tls handshake done after %d ms, record buffers in %d out %d",
              this, m_host.c_str(), (int)m_connected_ms, (int)m_ssl->MBEDTLS_PRIVATE(in_buf_len), (int)m_ssl->MBEDTLS_PRIVATE(out_buf_len));
#else
        trace("Session::lwip_connected: this=%p %s connected and tls handshake done after %d ms", this, m_host.c_str(), (int)m_connected_ms);
#endif
    }

    pump_write();

    if (!m_accepted && m_callback)
    {
        m_callback->on_connected();
    }

    if (OsLoop::alive(token) && (m_ssl != NULL) && (m_phase == Phase::Open) && mbedtls_ssl_check_pending(m_ssl))
    {
        do_read();
    }
}

void Session::do_read()
{
    // At most what lwIP hands over in one pbuf, so parsers see data split as on the board.
    uint8_t buffer[TCP_MSS];

    for (int round = 0; (round < 64) && (m_fd >= 0); ++round)
    {
        ssize_t len = 0;

        if (m_ssl != NULL)
        {
            int result = mbedtls_ssl_read(m_ssl, buffer, sizeof(buffer));

            if ((result == MBEDTLS_ERR_SSL_WANT_READ) || (result == MBEDTLS_ERR_SSL_WANT_WRITE))
            {
                return;
            }

            if ((result == 0) || (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) || (result == MBEDTLS_ERR_SSL_CONN_EOF))
            {
                len = 0;
            }
            else if (result < 0)
            {
                trace("Session::lwip_recv: this=%p tls read failed: %s", this, tls_error(result));
                fail_connection((result == MBEDTLS_ERR_NET_CONN_RESET) ? ERR_RST : ERR_ABRT, 0);
                return;
            }
            else
            {
                len = result;
            }
        }
        else
        {
            len = recv(m_fd, buffer, sizeof(buffer), 0);

            if (len < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                {
                    return;
                }

                fail_connection((errno == ECONNRESET) ? ERR_RST : ERR_ABRT, errno);
                return;
            }
        }

        if (len == 0)
        {
            trace("Session::lwip_recv: this=%p connection is closed by remote party.\n", this);
            close();
            return;
        }

        m_received += len;

        if (!deliver(buffer, len))
        {
            return;
        }
    }
}

bool Session::deliver(uint8_t *data, size_t len)
{
    if (!m_callback)
    {
        return true;
    }

    uint64_t token = m_token;

    m_processing = true;
    bool status = m_callback->on_recv(data, len);

    if (!OsLoop::alive(token))
    {
        // On the board lwip_recv() goes on to use the deleted session.
        trace("Session::lwip_recv: this=%p was deleted from inside its own on_recv(), a use after free on the board", this);
        return false;
    }

    if (m_closing || (m_fd < 0))
    {
        m_processing = false;
        if (m_callback)
        {
            m_callback->on_closed();
        }
        return false;
    }

    if (!status)
    {
        trace("Session::lwip_recv: this=%p closing, the handler refused %d bytes", this, (int)len);

        m_processing = false;
        close();
        return false;
    }

    m_processing = false;
    return true;
}

bool Session::is_connected()
{
    return (m_fd >= 0) && !m_closing && m_connected;
}
