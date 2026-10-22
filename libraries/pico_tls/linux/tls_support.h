/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"

mbedtls_ctr_drbg_context *tls_rng();
mbedtls_ssl_config *tls_create_client_config(const uint8_t *ca, size_t ca_len);
mbedtls_ssl_config *tls_create_server_config(const uint8_t *key, size_t key_len, const uint8_t *cert, size_t cert_len);

int tls_send(void *ctx, const unsigned char *buf, size_t len);
int tls_recv(void *ctx, unsigned char *buf, size_t len);

const char *tls_error(int error);

int listen_socket(uint16_t port, const char *what);
int accept_socket(int listen_fd, const char *what);

int local_port(int fd);
