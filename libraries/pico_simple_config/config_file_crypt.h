/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "mbedtls/gcm.h"
#include "os_fs.h"
#include "os_platform.h"

// The config file, encrypted with AES-256-GCM under a key derived from this board's unique id, so it only opens on the board that wrote it.
// [MAGIC "PCFG", 4][VERSION, 1][NONCE, 12][TAG, 16][CIPHERTEXT], all but the tag authenticated.
//
// Important: Right now this is mearly obfuscation to make a tiny bit harder to retrieve items, do not rely on this
//            This is hooked in to make it easier to migrate later to RP2350 OTP.
//            
class ConfigFileCrypt
{
public:
    static constexpr uint32_t MAGIC_LEN = 4;
    static constexpr uint32_t NONCE_LEN = 12;
    static constexpr uint32_t TAG_LEN = 16;
    static constexpr uint32_t AAD_LEN = MAGIC_LEN + 1 + NONCE_LEN;
    static constexpr uint32_t HEADER_LEN = AAD_LEN + TAG_LEN;

    ConfigFileCrypt();
    ~ConfigFileCrypt();

    ConfigFileCrypt(const ConfigFileCrypt&) = delete;
    ConfigFileCrypt& operator=(const ConfigFileCrypt&) = delete;

    uint8_t *read(const char *name, uint32_t max_len, uint32_t& size);
    bool write(const char *name, const uint8_t *data, uint32_t len);

private:
    static constexpr uint8_t MAGIC[MAGIC_LEN] = { 'P', 'C', 'F', 'G' };
    static constexpr uint8_t VERSION = 1;
    static constexpr uint32_t KEY_LEN = 32;
    static constexpr const char *KEY_INFO = "pico os config v1";

    bool make_key();
    bool crypt(bool encrypt, uint8_t *header, uint8_t *data, uint32_t len);

    mbedtls_gcm_context m_gcm;
    uint8_t m_key[KEY_LEN] = {};
    uint8_t m_id[OS_UNIQUE_ID_SIZE] = {};
    os_file_t m_file = {};
};
