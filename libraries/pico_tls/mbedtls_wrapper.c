/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "mbedtls_wrapper.h"

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include "pico_tls_common.h"

#ifdef __cplusplus
extern "C" {
#endif
    
int sha1(const unsigned char *input, size_t ilen, unsigned char output[20])
{
    return mbedtls_sha1(input, ilen, output);
}

int base64_encode(const unsigned char *src, size_t slen, unsigned char *dst, size_t dlen)
{
    size_t olen;

    int r = mbedtls_base64_encode(dst, dlen - 1, &olen, src, slen);
    if (0 != r) {
        return r;
    }
    
    dst[olen] = 0;
    return 0;
}

void mbedtls_debug_print(void *cookie, int level, const char *file, int line, const char *message) {
    trace("%s:%d %s\r\n", file, line, message);
}

#ifdef __cplusplus
}
#endif
