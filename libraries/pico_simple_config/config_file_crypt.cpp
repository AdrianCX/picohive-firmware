/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <string.h>

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

#include "config_file_crypt.h"
#include "pico_logger.h"

ConfigFileCrypt::ConfigFileCrypt()
{
    mbedtls_gcm_init(&m_gcm);
}

ConfigFileCrypt::~ConfigFileCrypt()
{
    mbedtls_gcm_free(&m_gcm);
    memset(m_key, 0, sizeof(m_key));
    memset(m_id, 0, sizeof(m_id));
}

bool ConfigFileCrypt::make_key()
{
    os_unique_id(m_id);

    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (sha256 == NULL)
    {
        return false;
    }

    if (mbedtls_hkdf(sha256, NULL, 0, m_id, sizeof(m_id), (const uint8_t *)KEY_INFO, strlen(KEY_INFO), m_key, KEY_LEN) != 0)
    {
        return false;
    }

    return (mbedtls_gcm_setkey(&m_gcm, MBEDTLS_CIPHER_ID_AES, m_key, KEY_LEN * 8) == 0);
}

bool ConfigFileCrypt::crypt(bool encrypt, uint8_t *header, uint8_t *data, uint32_t len)
{
    const uint8_t *nonce = header + MAGIC_LEN + 1;
    uint8_t *tag = header + AAD_LEN;

    if (encrypt)
    {
        return mbedtls_gcm_crypt_and_tag(&m_gcm, MBEDTLS_GCM_ENCRYPT, len, nonce, NONCE_LEN, header, AAD_LEN, data, data, TAG_LEN, tag) == 0;
    }

    return mbedtls_gcm_auth_decrypt(&m_gcm, len, nonce, NONCE_LEN, header, AAD_LEN, tag, TAG_LEN, data, data) == 0;
}

uint8_t *ConfigFileCrypt::read(const char *name, uint32_t max_len, uint32_t& size)
{
    size = 0;

    if (os_fs_open(&m_file, name, OS_FS_READ) != 0)
    {
        trace("config: no %s", name);
        return NULL;
    }

    int32_t file_size = os_fs_size(&m_file);
    if ((file_size < (int32_t)HEADER_LEN) || (file_size > (int32_t)(HEADER_LEN + max_len)))
    {
        os_fs_close(&m_file);
        trace("config: %s is %d bytes, not one of ours", name, (int)file_size);
        return NULL;
    }

    uint8_t *buffer = (uint8_t *)malloc(file_size);
    bool ok = (buffer != NULL) && (os_fs_read(&m_file, buffer, file_size) == file_size);
    os_fs_close(&m_file);

    ok = ok && (memcmp(buffer, MAGIC, MAGIC_LEN) == 0)
            && (buffer[MAGIC_LEN] == VERSION)
            && make_key()
            && crypt(false, buffer, buffer + HEADER_LEN, file_size - HEADER_LEN);

    if (!ok)
    {
        if (buffer != NULL)
        {
            memset(buffer, 0, file_size);
            free(buffer);
        }
        trace("config: %s does not decrypt (another board's, or damaged)", name);
        return NULL;
    }

    size = file_size;
    return buffer;
}

bool ConfigFileCrypt::write(const char *name, const uint8_t *data, uint32_t len)
{
    uint8_t *buffer = (uint8_t *)malloc(HEADER_LEN + len);

    if (buffer == NULL)
    {
        return false;
    }

    memcpy(buffer, MAGIC, MAGIC_LEN);
    buffer[MAGIC_LEN] = VERSION;

    uint8_t *nonce = buffer + MAGIC_LEN + 1;
    for (uint32_t i = 0; i < NONCE_LEN; i += 8)
    {
        uint64_t r = get_rand_64();
        memcpy(nonce + i, &r, ((NONCE_LEN - i) < 8) ? (NONCE_LEN - i) : 8);
    }

    memcpy(buffer + HEADER_LEN, data, len);

    bool ok = make_key() && crypt(true, buffer, buffer + HEADER_LEN, len);

    if (ok && (os_fs_open(&m_file, name, OS_FS_WRITE | OS_FS_CREATE | OS_FS_TRUNCATE) == 0))
    {
        ok = os_fs_write(&m_file, buffer, HEADER_LEN + len) == (int32_t)(HEADER_LEN + len);
        ok = (os_fs_close(&m_file) == 0) && ok;
    }
    else
    {
        ok = false;
    }

    memset(buffer, 0, HEADER_LEN + len);
    free(buffer);

    return ok;
}
