/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "mbedtls/x509_crt.h"

struct RootCertificate
{
    const char *name;
    const uint8_t *der;
    uint32_t der_len;
    uint32_t subject_at;
    uint32_t subject_len;
};

// Generated into the build by os/util/make_root_certificates.py from config/tls_domains.txt or the app's src/config/tls_domains.txt
extern const RootCertificate ROOT_CERTIFICATES[];
extern const uint32_t ROOT_CERTIFICATE_COUNT;

class RootCertificates
{
public:
    static int find(void *context, const mbedtls_x509_crt *child, mbedtls_x509_crt **candidates);
    static int check(void *context, mbedtls_x509_crt *crt, int depth, uint32_t *flags);
};
