/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>
#include "os_platform.h"
#include "mbedtls/platform_util.h"
#include "pico_helper.h"
#include "pico_logger.h"
#include "log_encryption.h"

LogEncryption LogEncryption::s_instance;

LogEncryption& LogEncryption::instance()
{
    return s_instance;
}

LogEncryption::LogEncryption()
{
    mbedtls_gcm_init(&m_gcm);
}

bool LogEncryption::set_key(const char *hex)
{
    m_enabled = false;
    mbedtls_gcm_free(&m_gcm);
    mbedtls_gcm_init(&m_gcm);

    if (hex == nullptr || hex[0] == 0)
    {
        return true;
    }

    uint8_t key[32];
    uint32_t len = (strlen(hex) == 64) ? 32 : 16;
    bool ok = strchr(hex, '-') == nullptr && PicoHelper::parse_hex(hex, key, len) &&
              mbedtls_gcm_setkey(&m_gcm, MBEDTLS_CIPHER_ID_AES, key, len * 8) == 0;
    mbedtls_platform_zeroize(key, sizeof(key));

    m_enabled = ok;
    return ok;
}

// A uuid goes in binary, anything else as its first 16 bytes.
void LogEncryption::set_device_id(const char *id)
{
    memset(m_device_id, 0, sizeof(m_device_id));
    if (!PicoHelper::parse_hex(id, m_device_id, sizeof(m_device_id)))
    {
        memset(m_device_id, 0, sizeof(m_device_id));
        strncpy((char *)m_device_id, id, sizeof(m_device_id));
    }
}

uint32_t LogEncryption::seal(uint32_t ip, const char *text, uint32_t len)
{
    if (len > MAX_TEXT)
    {
        len = MAX_TEXT;
    }

    memmove(this->text(), text, len);
    return seal_text(ip, len);
}

uint32_t LogEncryption::seal_text(uint32_t ip, uint32_t len)
{
    if (len > MAX_TEXT)
    {
        len = MAX_TEXT;
    }

    return seal_packet(m_packet, VERSION_LOG, ip, len);
}

uint32_t LogEncryption::seal_packet(uint8_t *packet, uint8_t version, uint32_t word, uint32_t len)
{
    if (!m_enabled)
    {
        return 0;
    }

    // A nonce must never repeat for a key: random per boot, counted within it.
    if (!m_boot_id_ready || m_sequence == UINT32_MAX)
    {
        uint64_t r = get_rand_64();
        memcpy(m_boot_id, &r, sizeof(m_boot_id));
        m_boot_id_ready = true;
        m_sequence = 0;
    }

    uint32_t seq = m_sequence++;
    packet[0] = version;
    memcpy(&packet[1], m_device_id, 16);
    memcpy(&packet[17], m_boot_id, 8);
    packet[25] = seq >> 24;
    packet[26] = seq >> 16;
    packet[27] = seq >> 8;
    packet[28] = seq;

    // encrypted in place
    uint8_t *body = &packet[HEADER_SIZE];
    memcpy(body, &word, 4);

    int ret = mbedtls_gcm_crypt_and_tag(&m_gcm, MBEDTLS_GCM_ENCRYPT, 4 + len,
                                        &packet[17], 12,
                                        packet, HEADER_SIZE,
                                        body, body,
                                        TAG_SIZE, &body[4 + len]);
    if (ret != 0)
    {
        return 0;
    }

    return HEADER_SIZE + 4 + len + TAG_SIZE;
}

extern "C" int set_logging_key(const char *hex)
{
    return LogEncryption::instance().set_key(hex) ? 1 : 0;
}

extern "C" void log_encryption_set_device_id(const char *id)
{
    LogEncryption::instance().set_device_id(id);
}

extern "C" int log_encryption_enabled()
{
    return LogEncryption::instance().enabled() ? 1 : 0;
}

extern "C" uint32_t log_encryption_seal(uint32_t ip, const char *text, uint32_t len, const uint8_t **packet)
{
    *packet = LogEncryption::instance().packet();
    return LogEncryption::instance().seal(ip, text, len);
}

extern "C" char *log_encryption_text(uint32_t *max_len)
{
    *max_len = LogEncryption::MAX_TEXT;
    return LogEncryption::instance().text();
}

extern "C" uint32_t log_encryption_seal_text(uint32_t ip, uint32_t len, const uint8_t **packet)
{
    *packet = LogEncryption::instance().packet();
    return LogEncryption::instance().seal_text(ip, len);
}
