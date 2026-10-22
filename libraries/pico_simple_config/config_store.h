/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "pico_mjson.h"

#ifndef CONFIG_MAX_JSON
#define CONFIG_MAX_JSON 2048
#endif

// The config as it is in flash, decrypted for as long as this lives; wiped and freed after.
class StoredConfig
{
public:
    StoredConfig();
    ~StoredConfig();

    StoredConfig(const StoredConfig&) = delete;
    StoredConfig& operator=(const StoredConfig&) = delete;

    bool loaded() const { return m_buffer != nullptr; }

    const JsonView json() const;

    // Store a field in config, call save to persist
    bool set_field(const char *section, const char *key, const char *value);
    bool set_field(const char *section, const char *key, uint32_t value);

    // Merge user provided config with existing stored one, a secret sent empty keeps the stored one, null clears it. Call save to persist.
    bool merge(const JsonView& incoming);

    // persist config
    bool save();

private:
    template <typename Write>
    bool rewrite_field(const char *section, const char *key, Write write);

    bool adopt(uint8_t *buffer, uint32_t len);
    static uint8_t *new_buffer();

    uint8_t *m_buffer = nullptr;
    uint32_t m_size = 0;
    bool m_changed = false;
};

class ConfigStore
{
public:
    static bool update(const char *json, uint32_t len);

    static uint32_t masked(char *out, uint32_t size);

private:
    friend class StoredConfig;

    static bool is_secret(const JsonView& key);
    static bool merge_object(const JsonView& stored, const JsonView& incoming, JsonWriter& out, uint32_t depth);
    static bool mask_object(const JsonView& stored, JsonWriter& out, uint32_t depth);
    static bool write_config(const char *json, uint32_t len);
    static bool write_file(const char *json, uint32_t len);
};
