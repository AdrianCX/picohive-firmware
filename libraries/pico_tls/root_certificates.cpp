/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "root_certificates.h"

#include <string.h>

#include "mbedtls/platform.h"
#include "pico_logger.h"
#include "pico_tls_build_date.h"

int RootCertificates::find(void *context, const mbedtls_x509_crt *child, mbedtls_x509_crt **candidates)
{
    *candidates = NULL;

    for (uint32_t index = 0; index < ROOT_CERTIFICATE_COUNT; ++index)
    {
        const RootCertificate& root = ROOT_CERTIFICATES[index];

        if ((child->issuer_raw.len != root.subject_len) || (memcmp(child->issuer_raw.p, root.der + root.subject_at, root.subject_len) != 0))
        {
            continue;
        }

        mbedtls_x509_crt *crt = (mbedtls_x509_crt *)mbedtls_calloc(1, sizeof(mbedtls_x509_crt));

        if (crt == NULL)
        {
            return MBEDTLS_ERR_X509_ALLOC_FAILED;
        }

        mbedtls_x509_crt_init(crt);

        int error = mbedtls_x509_crt_parse_der_nocopy(crt, root.der, root.der_len);

        if (error != 0)
        {
            trace("tls: built-in root [%s] did not parse: -0x%x", root.name, -error);
            mbedtls_x509_crt_free(crt);
            mbedtls_free(crt);
            continue;
        }

        crt->next = *candidates;
        *candidates = crt;
    }

    return 0;
}

int RootCertificates::check(void *context, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    if (expired(crt))
    {
        *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    }

    if (*flags != 0)
    {
        trace("tls: certificate %d of the chain refused, flags 0x%x:%s%s%s%s", depth, (unsigned int)*flags,
              (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) ? " no built-in root signs it" : "",
              (*flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) ? " not issued for this host" : "",
              (*flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) ? " outside its dates" : "",
              (*flags & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK | MBEDTLS_X509_BADCERT_BAD_KEY)) ? " algorithm or key not allowed" : "");
    }

    return 0;
}

bool RootCertificates::expired(const mbedtls_x509_crt *crt)
{
    const mbedtls_x509_time& to = crt->valid_to;

    return ((to.year * 10000) + (to.mon * 100) + to.day) < PICO_TLS_EXPIRED_BEFORE;
}
