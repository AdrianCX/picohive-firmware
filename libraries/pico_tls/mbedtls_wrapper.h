/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef MBEDTLS_WRAPPER_H
#define MBEDTLS_WRAPPER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
    
int sha1(const unsigned char *input, size_t ilen, unsigned char output[20]);
int base64_encode(const unsigned char *src, size_t slen, unsigned char *dst, size_t dlen);

void mbedtls_debug_print(void *cookie, int level, const char *file, int line, const char *message);

#ifdef __cplusplus
}
#endif

#endif
