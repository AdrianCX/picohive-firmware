/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "tls_support.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

#include "pico_logger.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>

#include "os_platform.h"
#include "os_linux.h"
#include "session.h"

mbedtls_ctr_drbg_context *tls_rng()
{
    static bool ready = false;
    static mbedtls_entropy_context entropy;
    static mbedtls_ctr_drbg_context drbg;

    if (!ready)
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);

        int error = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"picohive-linux", 14);
        if (error != 0)
        {
            fail("tls_rng: seeding failed: %s", tls_error(error));
        }
        ready = true;
    }

    return &drbg;
}

mbedtls_ssl_config *tls_create_client_config(const uint8_t *ca, size_t ca_len)
{
    mbedtls_ssl_config *conf = new mbedtls_ssl_config;
    mbedtls_ssl_config_init(conf);

    int error = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (error != 0)
    {
        trace("tls_create_client_config: defaults failed: %s", tls_error(error));
        mbedtls_ssl_config_free(conf);
        delete conf;
        return NULL;
    }

    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, tls_rng());
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

    if (ca != NULL)
    {
        mbedtls_x509_crt *chain = new mbedtls_x509_crt;
        mbedtls_x509_crt_init(chain);

        error = mbedtls_x509_crt_parse(chain, ca, ca_len);
        if (error != 0)
        {
            trace("tls_create_client_config: the CA did not parse: %s", tls_error(error));
            mbedtls_x509_crt_free(chain);
            delete chain;
            mbedtls_ssl_config_free(conf);
            delete conf;
            return NULL;
        }

        mbedtls_ssl_conf_ca_chain(conf, chain, NULL);
    }

    return conf;
}

mbedtls_ssl_config *tls_create_server_config(const uint8_t *key, size_t key_len, const uint8_t *cert, size_t cert_len)
{
    mbedtls_ssl_config *conf = new mbedtls_ssl_config;
    mbedtls_ssl_config_init(conf);

    mbedtls_x509_crt *own_cert = new mbedtls_x509_crt;
    mbedtls_x509_crt_init(own_cert);

    mbedtls_pk_context *own_key = new mbedtls_pk_context;
    mbedtls_pk_init(own_key);

    int error = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);

    if (error == 0)
    {
        error = mbedtls_x509_crt_parse(own_cert, cert, cert_len);
        if (error != 0)
        {
            trace("tls_create_server_config: the certificate did not parse: %s", tls_error(error));
        }
    }

    if (error == 0)
    {
        error = mbedtls_pk_parse_key(own_key, key, key_len, NULL, 0, mbedtls_ctr_drbg_random, tls_rng());
        if (error != 0)
        {
            trace("tls_create_server_config: the key did not parse: %s", tls_error(error));
        }
    }

    if (error == 0)
    {
        mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, tls_rng());
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
        error = mbedtls_ssl_conf_own_cert(conf, own_cert, own_key);
    }

    if (error != 0)
    {
        trace("tls_create_server_config: failed: %s", tls_error(error));
        mbedtls_pk_free(own_key);
        delete own_key;
        mbedtls_x509_crt_free(own_cert);
        delete own_cert;
        mbedtls_ssl_config_free(conf);
        delete conf;
        return NULL;
    }

    return conf;
}

int tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;

    while (true)
    {
        ssize_t n = ::send(fd, buf, len, MSG_NOSIGNAL);

        if (n >= 0)
        {
            return (int)n;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        if ((errno == ECONNRESET) || (errno == EPIPE))
        {
            return MBEDTLS_ERR_NET_CONN_RESET;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
}

int tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;

    while (true)
    {
        ssize_t n = ::recv(fd, buf, len, 0);

        if (n >= 0)
        {
            return (int)n;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        if (errno == ECONNRESET)
        {
            return MBEDTLS_ERR_NET_CONN_RESET;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
}

const char *tls_error(int error)
{
    static char text[160];
    char description[128] = {};

    mbedtls_strerror(error, description, sizeof(description));
    snprintf(text, sizeof(text), "-0x%04x %s", (unsigned int)-error, description);
    return text;
}

int listen_socket(uint16_t port, const char *what)
{
    uint16_t actual = os_map_port(port);

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        trace("%s::listen: socket failed: %s", what, strerror(errno));
        return -1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(actual);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        trace("Bind error: %s, port %d (asked for %d)%s\r\n", strerror(errno), (int)actual, (int)port, (errno == EACCES) ? ": needs privileges, leave out --port-offset 0 or sudo sysctl net.ipv4.ip_unprivileged_port_start=80" : "");
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 8) != 0)
    {
        trace("Listen failed: %s\r\n", strerror(errno));
        ::close(fd);
        return -1;
    }

    if (actual != port)
    {
        trace("%s::listen: port %d is %d here", what, (int)port, (int)actual);
    }

    return fd;
}

int accept_socket(int listen_fd, const char *what)
{
    int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (fd < 0)
    {
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR))
        {
            trace("%s::http_accept: accept failed: %s", what, strerror(errno));
        }
        return -1;
    }

    if (Session::get_num_accepted() >= MAX_ACCEPTED_SESSIONS)
    {
        trace("%s::http_accept: refused, active connections: %d\n", what, Session::get_num_accepted());

        // An abort: reset, not a polite close.
        struct linger abort_close = {1, 0};
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &abort_close, sizeof(abort_close));
        ::close(fd);
        return -1;
    }

    return fd;
}

int local_port(int fd)
{
    struct sockaddr_in address = {};
    socklen_t len = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &len) != 0)
    {
        return -1;
    }
    return ntohs(address.sin_port);
}
