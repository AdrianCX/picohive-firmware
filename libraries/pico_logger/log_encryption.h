/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _LOG_ENCRYPTION_H
#define _LOG_ENCRYPTION_H

#include <stdint.h>

// Encrypted log packet, AES-GCM, see util/log_decrypt.py for the receiving end:
//
//   0   1   version (1)
//   1  16   device id, the uuid in binary
//  17   8   boot id, random per boot          ┐ nonce
//  25   4   sequence, big endian, per boot    ┘
//  29   4   ip address                        ┐ encrypted
//  33   n   log line                          ┘
//  ..  16   tag
//
// Bytes 0..28 are authenticated as well, so the readable header can't be changed or moved to another device.

#ifdef __cplusplus
extern "C" {
#endif

// Used by pico_logger.c, the key itself is set with set_logging_key() in pico_logger.h
void log_encryption_set_device_id(const char *id);
int log_encryption_enabled();
// Returns the packet length and points packet at it, 0 when it failed.
uint32_t log_encryption_seal(uint32_t ip, const char *text, uint32_t len, const uint8_t **packet);

// The same without a copy: the line is written straight into the packet then sealed where it is. Usable as a plain buffer when encryption is off.
char *log_encryption_text(uint32_t *max_len);
uint32_t log_encryption_seal_text(uint32_t ip, uint32_t len, const uint8_t **packet);

#ifdef __cplusplus
}

#include "mbedtls/gcm.h"

class LogEncryption
{
public:
    static const uint32_t HEADER_SIZE = 29;
    static const uint32_t TAG_SIZE = 16;
    // Fits one ethernet frame, no ip fragments.
    static const uint32_t MAX_PACKET = 1472;
    static const uint32_t MAX_TEXT = MAX_PACKET - HEADER_SIZE - 4 - TAG_SIZE;

    static const uint8_t VERSION_LOG = 1;
    static const uint8_t VERSION_CORE_DUMP = 2;

    static LogEncryption& instance();

    // 32 hex characters for AES-128, 64 for AES-256, empty turns encryption off.
    bool set_key(const char *hex);
    void set_device_id(const char *id);

    bool enabled() const { return m_enabled; }
    uint32_t seal(uint32_t ip, const char *text, uint32_t len);
    const uint8_t *packet() const { return m_packet; }

    // Where the line goes in the packet, MAX_TEXT characters and a NUL.
    char *text() { return (char *)&m_packet[HEADER_SIZE + 4]; }
    uint32_t seal_text(uint32_t ip, uint32_t len);

    uint32_t seal_packet(uint8_t *packet, uint8_t version, uint32_t word, uint32_t len);

private:
    LogEncryption();

    static LogEncryption s_instance;

    mbedtls_gcm_context m_gcm;
    bool m_boot_id_ready = false;
    bool m_enabled = false;

    uint8_t m_device_id[16] = {};
    uint8_t m_boot_id[8] = {};
    uint32_t m_sequence = 0;

    uint8_t m_packet[MAX_PACKET] = {};
};

#endif

#endif
