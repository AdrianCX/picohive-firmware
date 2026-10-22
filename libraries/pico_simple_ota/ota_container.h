/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"

// Firmware arrives wrapped in a container signed with developer key (the application's keys/versions/<n>/):
//
//   .uf2.encrypted  [MAGIC][HEADER_SIZE][KEYLEN][EPHEMERAL_KEY]
//                   [TAG][BUILD_ID][BOARD][SIGLEN][SIG][CIPHERTEXT]
//
// Encryption:
// - It keeps the firmware unreadable in transit - the MQTT broker forwarding it only ever sees ciphertext
// - It stays secret only in transit, the plain UF2 is what ends up in flash, and the key that unwraps it is in the firmware.
//
// Signing:
// - Done with a developer key, private key stays hidden, public key is on device to check firmware
//
#define OTA_ENCRYPTED_MAGIC "ENCRUF2\n"
#define OTA_MAGIC_LEN 8
#define OTA_PREFIX_LEN (OTA_MAGIC_LEN + 4)
#define OTA_KDF_SALT "pico_os ota v1"
#define OTA_KDF_SALT_LEN (sizeof(OTA_KDF_SALT) - 1)
#define OTA_SHARED_SECRET_LEN 32
#define OTA_AES_KEY_LEN 32
#define OTA_IV_LEN 12
#define OTA_SECRET_LEN (OTA_AES_KEY_LEN + OTA_IV_LEN)
#define OTA_TAG_LEN 16
#define OTA_BUILD_ID_LEN 20
#define OTA_BOARD_LEN 16
#define OTA_DECRYPT_CHUNK 512
#define OTA_DECRYPT_BUFFER (OTA_DECRYPT_CHUNK + 16)

#define OTA_MAX_KEY_LEN 133
#define OTA_MAX_SIG_LEN 512

#define OTA_MAX_HEADER_LEN (OTA_PREFIX_LEN + 4 + OTA_MAX_KEY_LEN + OTA_TAG_LEN + \
                            OTA_BUILD_ID_LEN + OTA_BOARD_LEN + 4 + OTA_MAX_SIG_LEN)

// Where the plaintext UF2 goes as it is unwrapped.
class IOtaPlaintextSink
{
public:
    virtual bool onPlaintext(uint8_t *data, size_t len) = 0;

protected:
    ~IOtaPlaintextSink() = default;
};

class OtaContainer
{
public:
    OtaContainer();
    ~OtaContainer();

    void reset();

    // Strips the header, decrypts if needed, hands the UF2 to the sink
	// False - the container is malformed, not signed
    bool feed(const uint8_t *data, size_t len, IOtaPlaintextSink& sink);

    // MAC and signature over everything fed so far, call once the upload has finished
    bool verify();

    // Build id of the incoming firmware as hex.
    const char *buildId() const { return &m_buildId[0]; }
    const char *lastError() const { return m_error; }

private:
    bool takeHeader(const uint8_t *&data, size_t& len);
    bool sizeHeader();
    bool parseHeader();
    bool deriveSecret(const uint8_t *ephemeral, uint32_t ephemeralLen, uint8_t *secret);
    bool checkSignature();

    const uint8_t *signature() const { return &m_header[m_signatureOffset]; }

    uint8_t m_header[OTA_MAX_HEADER_LEN] = {};
    uint32_t m_headerIndex = 0;
    uint32_t m_headerLen = 0;
    uint32_t m_signatureOffset = 0;
    uint32_t m_signatureLen = 0;
    uint32_t m_signedHeaderLen = 0;
    uint32_t m_tagOffset = 0;
    uint32_t m_payloadLen = 0;

    char m_buildId[OTA_BUILD_ID_LEN * 2 + 1] = {};
    char m_errorText[96] = {};

    mbedtls_sha256_context m_sha256;
    mbedtls_gcm_context m_gcm;
    uint8_t m_plain[OTA_DECRYPT_BUFFER] = {};

    bool m_headerParsed = false;
    bool m_gcmStarted = false;
    bool m_verified = false;
    bool m_verifyDone = false;
    bool m_failed = false;
    const char *m_error = nullptr;
};
