/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"

#include "os_platform.h"
#include "pico_logger.h"

#include "ota_container.h"
#include "ota_signing_key.h"
#include "ota_encryption_key.h"

#ifndef PICO_OS_BOARD_NAME
#error "PICO_OS_BOARD_NAME is set by signing.cmake; pico_simple_ota cannot check what a build is for without it"
#endif

#define OTA_MIN_ENCRYPTED_HEADER (OTA_PREFIX_LEN + 4 + 1 + OTA_TAG_LEN + OTA_BUILD_ID_LEN + OTA_BOARD_LEN + 4 + 1)

static uint32_t read_le32(const uint8_t *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int ota_rng(void *, unsigned char *output, size_t len)
{
    while (len > 0)
    {
        uint64_t value = get_rand_64();
        size_t count = (len < sizeof(value)) ? len : sizeof(value);

        memcpy(output, &value, count);
        output += count;
        len -= count;
    }

    return 0;
}

static uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

static bool same_tag(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

OtaContainer::OtaContainer()
{
    mbedtls_sha256_init(&m_sha256);
    mbedtls_gcm_init(&m_gcm);
    reset();
}

OtaContainer::~OtaContainer()
{
    mbedtls_sha256_free(&m_sha256);
    mbedtls_gcm_free(&m_gcm);
    mbedtls_platform_zeroize(&m_plain[0], sizeof(m_plain));
}

void OtaContainer::reset()
{
    if (m_gcmStarted)
    {
        mbedtls_gcm_free(&m_gcm);
        mbedtls_gcm_init(&m_gcm);
        m_gcmStarted = false;
    }

    mbedtls_sha256_starts(&m_sha256, 0);

    m_headerIndex = 0;
    m_headerLen = 0;
    m_signatureOffset = 0;
    m_signatureLen = 0;
    m_signedHeaderLen = 0;
    m_tagOffset = 0;
    m_payloadLen = 0;
    m_buildId[0] = '\0';
    m_headerParsed = false;
    m_verified = false;
    m_verifyDone = false;
    m_failed = false;
    m_error = nullptr;
}

bool OtaContainer::feed(const uint8_t *data, size_t len, IOtaPlaintextSink& sink)
{
    if (m_failed)
    {
        return false;
    }

    if (m_verifyDone)
    {
        trace("OtaContainer::feed: this=%p %u more bytes after the end of the firmware", this, (unsigned)len);
        m_error = "data after the end of the firmware";
        m_failed = true;
        return false;
    }

    if (!takeHeader(data, len))
    {
        m_failed = true;
        return false;
    }

    if (len == 0)
    {
        return true;
    }

    m_payloadLen += len;

    mbedtls_sha256_update(&m_sha256, data, len);

    while (len > 0)
    {
        size_t piece = (len < OTA_DECRYPT_CHUNK) ? len : OTA_DECRYPT_CHUNK;
        size_t plainLen = 0;

        if (mbedtls_gcm_update(&m_gcm, data, piece, &m_plain[0], sizeof(m_plain), &plainLen) != 0)
        {
            trace("OtaContainer::feed: this=%p decryption failed", this);
            m_error = "could not decrypt the firmware";
            m_failed = true;
            return false;
        }

        data += piece;
        len -= piece;

        if ((plainLen > 0) && !sink.onPlaintext(&m_plain[0], plainLen))
        {
            return false;
        }
    }

    return true;
}

bool OtaContainer::takeHeader(const uint8_t *&data, size_t& len)
{
    while (!m_headerParsed)
    {
        uint32_t want = (m_headerLen != 0) ? m_headerLen : OTA_PREFIX_LEN;

        size_t count = std::min<size_t>(len, want - m_headerIndex);
        memcpy(&m_header[m_headerIndex], data, count);
        m_headerIndex += count;
        data += count;
        len -= count;

        if (m_headerIndex < want)
        {
            return true;
        }

        if (m_headerLen == 0)
        {
            if (!sizeHeader())
            {
                return false;
            }
        }
        else
        {
            if (!parseHeader())
            {
                return false;
            }
            m_headerParsed = true;
        }
    }

    return true;
}

bool OtaContainer::sizeHeader()
{
    if (memcmp(&m_header[0], OTA_ENCRYPTED_MAGIC, OTA_MAGIC_LEN) != 0)
    {
        trace("OtaContainer::sizeHeader: this=%p unknown magic [%02x %02x %02x %02x]", this, m_header[0], m_header[1], m_header[2], m_header[3]);
        m_error = "not an OTA firmware, send the .uf2.encrypted";
        return false;
    }

    m_headerLen = read_le32(&m_header[OTA_MAGIC_LEN]);

    trace("OtaContainer::sizeHeader: this=%p %u byte header", this, m_headerLen);

    if ((m_headerLen < OTA_MIN_ENCRYPTED_HEADER) || (m_headerLen > OTA_MAX_HEADER_LEN))
    {
        trace("OtaContainer::sizeHeader: this=%p header length %u out of range", this, m_headerLen);
        m_error = "bad header length";
        return false;
    }

    return true;
}

bool OtaContainer::parseHeader()
{
    uint32_t offset = OTA_PREFIX_LEN;

    uint32_t keyLen = read_le32(&m_header[offset]);
    if ((keyLen == 0) || (keyLen > OTA_MAX_KEY_LEN))
    {
        trace("OtaContainer::parseHeader: this=%p ephemeral key length %u out of range", this, keyLen);
        m_error = "bad ephemeral key length";
        return false;
    }

    offset += 4;
    uint32_t ephemeralOffset = offset;
    offset += keyLen;
    m_tagOffset = offset;
    offset += OTA_TAG_LEN;

    if ((offset + OTA_BUILD_ID_LEN + OTA_BOARD_LEN + 4) > m_headerLen)
    {
        trace("OtaContainer::parseHeader: this=%p header of %u bytes is too short for its fields", this, m_headerLen);
        m_error = "malformed header";
        return false;
    }

    const uint8_t *buildId = &m_header[offset];
    offset += OTA_BUILD_ID_LEN;

    const char *board = (const char *)&m_header[offset];
    uint32_t boardLen = strnlen(board, OTA_BOARD_LEN);
    offset += OTA_BOARD_LEN;

    m_signedHeaderLen = offset;
    m_signatureLen = read_le32(&m_header[offset]);
    m_signatureOffset = offset + 4;

    if ((m_signatureLen == 0) || (m_signatureLen > OTA_MAX_SIG_LEN) ||
        (m_signatureOffset + m_signatureLen != m_headerLen))
    {
        trace("OtaContainer::parseHeader: this=%p signature of %u bytes at %u does not fit a %u byte header", this, m_signatureLen, m_signatureOffset, m_headerLen);
        m_error = "bad signature length";
        return false;
    }

    for (uint32_t i = 0; i < OTA_BUILD_ID_LEN; ++i)
    {
        snprintf(&m_buildId[i * 2], 3, "%02x", buildId[i]);
    }

    if ((boardLen != strlen(PICO_OS_BOARD_NAME)) || (memcmp(board, PICO_OS_BOARD_NAME, boardLen) != 0))
    {
        trace("OtaContainer::parseHeader: this=%p built for %.*s, this is " PICO_OS_BOARD_NAME, this, (int)boardLen, board);
        snprintf(&m_errorText[0], sizeof(m_errorText), "built for %.*s, this is " PICO_OS_BOARD_NAME, (int)boardLen, board);
        m_error = &m_errorText[0];
        return false;
    }

    trace("OtaContainer::parseHeader: this=%p firmware for " PICO_OS_BOARD_NAME ", build %s", this, &m_buildId[0]);

    mbedtls_sha256_update(&m_sha256, &m_header[0], m_signedHeaderLen);

    trace("OtaContainer::parseHeader: this=%p deriving the firmware key from a %u byte ephemeral key", this, keyLen);

    uint32_t started = now_ms();

    uint8_t secret[OTA_SECRET_LEN];
    if (!deriveSecret(&m_header[ephemeralOffset], keyLen, &secret[0]))
    {
        mbedtls_platform_zeroize(&secret[0], sizeof(secret));
        return false;
    }

    bool ok = (mbedtls_gcm_setkey(&m_gcm, MBEDTLS_CIPHER_ID_AES, &secret[0], OTA_AES_KEY_LEN * 8) == 0);

    if (ok)
    {
        m_gcmStarted = true;
        ok = (mbedtls_gcm_starts(&m_gcm, MBEDTLS_GCM_DECRYPT, &secret[OTA_AES_KEY_LEN], OTA_IV_LEN) == 0);
    }

    if (ok)
    {
        trace("OtaContainer::parseHeader: this=%p key derived and cipher ready in %u ms, receiving firmware", this, now_ms() - started);
    }
    else
    {
        trace("OtaContainer::parseHeader: this=%p could not set up the cipher", this);
        m_error = "could not set up decryption";
    }

    mbedtls_platform_zeroize(&secret[0], sizeof(secret));
    return ok;
}

bool OtaContainer::deriveSecret(const uint8_t *ephemeral, uint32_t ephemeralLen, uint8_t *secret)
{
    update_watchdog();

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    mbedtls_ecp_point peer;
    mbedtls_ecp_point_init(&peer);

    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);

    mbedtls_mpi d;
    mbedtls_mpi_init(&d);

    mbedtls_ecp_point Q;
    mbedtls_ecp_point_init(&Q);

    mbedtls_mpi shared;
    mbedtls_mpi_init(&shared);

    uint8_t z[OTA_SHARED_SECRET_LEN] = {};
    bool ok = false;

    int rc = mbedtls_pk_parse_key(&pk, (const unsigned char *)OTA_ENCRYPTION_PRIVATE_KEY, sizeof(OTA_ENCRYPTION_PRIVATE_KEY), nullptr, 0, ota_rng, nullptr);

    trace("OtaContainer::deriveSecret: this=%p embedded key parsed, rc=%d, running ecdh", this, rc);

    if (rc != 0)
    {
        trace("OtaContainer::deriveSecret: this=%p embedded private key does not parse, rc=-0x%x", this, -rc);
        m_error = "embedded decryption key unusable";
    }
    else if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY))
    {
        trace("OtaContainer::deriveSecret: this=%p embedded private key is not an ec key", this);
        m_error = "embedded decryption key is not an ec key";
    }
    else if ((rc = mbedtls_ecp_export(mbedtls_pk_ec(pk), &grp, &d, &Q)) != 0)
    {
        trace("OtaContainer::deriveSecret: this=%p embedded key does not export, rc=-0x%x", this, -rc);
        m_error = "embedded decryption key unusable";
    }
    else
    {
        if ((rc = mbedtls_ecp_point_read_binary(&grp, &peer, ephemeral, ephemeralLen)) != 0)
        {
            trace("OtaContainer::deriveSecret: this=%p the %u byte ephemeral key does not read, rc=-0x%x", this, ephemeralLen, -rc);
            m_error = "firmware encrypted to another key";
        }
        else if ((rc = mbedtls_ecp_check_pubkey(&grp, &peer)) != 0)
        {
            trace("OtaContainer::deriveSecret: this=%p the ephemeral key is not on this curve, rc=-0x%x", this, -rc);
            m_error = "firmware encrypted to another key";
        }
        else if ((rc = mbedtls_ecdh_compute_shared(&grp, &shared, &peer, &d, ota_rng, nullptr)) != 0)
        {
            trace("OtaContainer::deriveSecret: this=%p ecdh failed, rc=-0x%x", this, -rc);
            m_error = "firmware encrypted to another key";
        }
        else if ((rc = mbedtls_mpi_write_binary(&shared, &z[0], sizeof(z))) != 0)
        {
            trace("OtaContainer::deriveSecret: this=%p shared secret does not fit %u bytes, rc=-0x%x", this, (unsigned)sizeof(z), -rc);
            m_error = "firmware encrypted to another key";
        }
        else
        {
            const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

            rc = (sha256 == nullptr) ? -1 : mbedtls_hkdf(sha256,
                                                         (const unsigned char *)OTA_KDF_SALT, OTA_KDF_SALT_LEN,
                                                         &z[0], sizeof(z),
                                                         ephemeral, ephemeralLen,
                                                         secret, OTA_SECRET_LEN);
            if (rc != 0)
            {
                trace("OtaContainer::deriveSecret: this=%p hkdf failed, rc=-0x%x", this, -rc);
                m_error = "could not derive the firmware key";
            }
            else
            {
                ok = true;
            }
        }
    }

    mbedtls_platform_zeroize(&z[0], sizeof(z));
    mbedtls_mpi_free(&shared);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_point_free(&peer);
    mbedtls_ecp_group_free(&grp);
    mbedtls_pk_free(&pk);
    update_watchdog();
    return ok;
}

bool OtaContainer::verify()
{
    if (m_verifyDone || m_failed)
    {
        return m_verified;
    }

    if (!m_headerParsed || (m_payloadLen == 0))
    {
        trace("OtaContainer::verify: this=%p incomplete, header %u/%u, payload %u", this, m_headerIndex, m_headerLen, m_payloadLen);
        m_error = "bad signature, binary incomplete";
        return false;
    }

    m_verifyDone = true;

    uint32_t started = now_ms();
    trace("OtaContainer::verify: this=%p checking tag and signature over %u bytes", this, m_payloadLen);

    if ((m_payloadLen % 16) != 0)
    {
        trace("OtaContainer::verify: this=%p %u bytes is not whole cipher blocks", this, m_payloadLen);
        m_error = "bad tag, the firmware is truncated";
        return false;
    }

    uint8_t tag[OTA_TAG_LEN];
    uint8_t leftover[16];
    size_t leftoverLen = 0;

    if (mbedtls_gcm_finish(&m_gcm, &leftover[0], sizeof(leftover), &leftoverLen, &tag[0], sizeof(tag)) != 0)
    {
        trace("OtaContainer::verify: this=%p could not finish decryption", this);
        m_error = "bad tag, the firmware was altered in transit";
        return false;
    }

    if (leftoverLen != 0)
    {
        trace("OtaContainer::verify: this=%p %u bytes past the last whole block", this, (unsigned)leftoverLen);
        m_error = "bad tag, the firmware is truncated";
        return false;
    }

    if (!same_tag(&tag[0], &m_header[m_tagOffset], OTA_TAG_LEN))
    {
        trace("OtaContainer::verify: this=%p the firmware does not match its tag", this);
        m_error = "bad tag, the firmware was altered in transit";
        return false;
    }

    trace("OtaContainer::verify: this=%p tag ok after %u ms, checking the signature", this, now_ms() - started);

    m_verified = checkSignature();
    return m_verified;
}

bool OtaContainer::checkSignature()
{
    uint8_t hash[32];
    mbedtls_sha256_finish(&m_sha256, &hash[0]);

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    bool ok = false;
    int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)OTA_SIGNING_PUBLIC_KEY, sizeof(OTA_SIGNING_PUBLIC_KEY));

    if (rc != 0)
    {
        trace("OtaContainer::checkSignature: this=%p embedded public key does not parse, rc=-0x%x", this, -rc);
        m_error = "bad signature, embedded public key unusable";
    }
    else if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA))
    {
        trace("OtaContainer::checkSignature: this=%p embedded public key is not RSA", this);
        m_error = "bad signature, embedded public key is not RSA";
    }
    else if ((rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, &hash[0], sizeof(hash), signature(), m_signatureLen)) != 0)
    {
        trace("OtaContainer::checkSignature: this=%p bad signature, rc=-0x%x", this, -rc);
        m_error = "bad signature";
    }
    else
    {
        trace("OtaContainer::checkSignature: this=%p signature ok over %u bytes of ciphertext", this, m_payloadLen);
        update_watchdog();
        ok = true;
    }

    mbedtls_pk_free(&pk);
    return ok;
}
