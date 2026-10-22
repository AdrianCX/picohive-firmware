/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _PICO_TLS_COMMON_H
#define _PICO_TLS_COMMON_H

#include "pico_logger.h"

#define MBEDTLS_CACHE_MAX 10

#ifndef TLS_HANDSHAKE_TIMEOUT_MS
#define TLS_HANDSHAKE_TIMEOUT_MS 30000
#endif

#ifndef TLS_HANDSHAKE_QUEUE_SIZE
#define TLS_HANDSHAKE_QUEUE_SIZE 8
#endif

#ifndef TLS_VERIFY_SERVERS
#define TLS_VERIFY_SERVERS 1
#endif

#endif
