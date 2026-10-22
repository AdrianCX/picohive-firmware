/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <string.h>

#include <new>

#include "config_file_crypt.h"
#include "config_store.h"
#include "os_fs.h"
#include "pico_logger.h"

static const char *CONFIG_FILE = "config.enc";
static const uint32_t HEADER_LEN = ConfigFileCrypt::HEADER_LEN;

StoredConfig::StoredConfig()
{
    ConfigFileCrypt *crypt = new (std::nothrow) ConfigFileCrypt();
    if (crypt == nullptr)
    {
        return;
    }

    m_buffer = crypt->read(CONFIG_FILE, CONFIG_MAX_JSON, m_size);
    delete crypt;

    if (m_buffer == nullptr)
    {
        trace("config: starting unconfigured");
    }
}

StoredConfig::~StoredConfig()
{
    if (m_buffer != nullptr)
    {
        memset(m_buffer, 0, m_size);
        free(m_buffer);
    }
}

const JsonView StoredConfig::json() const
{
    if (m_buffer == nullptr)
    {
        return JsonView("", 0);
    }

    return JsonView((const char *)m_buffer + HEADER_LEN, m_size - HEADER_LEN);
}

bool ConfigStore::write_file(const char *json, uint32_t len)
{
    ConfigFileCrypt *crypt = new (std::nothrow) ConfigFileCrypt();
    bool ok = (crypt != nullptr) && crypt->write(CONFIG_FILE, (const uint8_t *)json, len);
    delete crypt;

    if (!ok)
    {
        trace("config: writing %s failed", CONFIG_FILE);
    }

    return ok;
}

// Formats the filesystem and tries again if the first write fails.
bool ConfigStore::write_config(const char *json, uint32_t len)
{
    if (!write_file(json, len))
    {
        trace("config: write failed, formatting and retrying");
        os_fs_format();

        if (!write_file(json, len))
        {
            return false;
        }
    }

    trace("config: saved, %u bytes", (unsigned int)len);
    return true;
}

bool StoredConfig::save()
{
    if (!m_changed)
    {
        trace("config: unchanged, not written");
        return true;
    }

    const JsonView current = json();

    if (!ConfigStore::write_config(current.data(), current.length()))
    {
        return false;
    }

    m_changed = false;
    return true;
}

static const uint32_t BUFFER_CAPACITY = HEADER_LEN + CONFIG_MAX_JSON + 1;

uint8_t *StoredConfig::new_buffer()
{
    return (uint8_t *)calloc(1, BUFFER_CAPACITY);
}

bool StoredConfig::adopt(uint8_t *buffer, uint32_t len)
{
    const JsonView current = json();

    if ((len == 0) || (len > CONFIG_MAX_JSON))
    {
        memset(buffer, 0, BUFFER_CAPACITY);
        free(buffer);
        return false;
    }

    if ((current.length() != len) || (memcmp(current.data(), buffer + HEADER_LEN, len) != 0))
    {
        m_changed = true;
    }

    if (m_buffer != nullptr)
    {
        memset(m_buffer, 0, m_size);
        free(m_buffer);
    }

    m_buffer = buffer;
    m_size = HEADER_LEN + len;

    return true;
}

// The config with section.key replaced (or added) by what write puts there.
template <typename Write>
bool StoredConfig::rewrite_field(const char *section, const char *key, Write write)
{
    uint8_t *buffer = new_buffer();
    if (buffer == NULL)
    {
        return false;
    }

    const JsonView current = json();

    JsonWriter writer((char *)buffer + HEADER_LEN, BUFFER_CAPACITY - HEADER_LEN);
    writer.begin_object();

    bool found = false;

    if (current.type() == JsonType::Object)
    {
        for (const JsonEntry& entry : current)
        {
            if (!entry.key.equals(section))
            {
                writer.key(entry.key).add_raw(NULL, entry.value);
                continue;
            }

            found = true;
            writer.key(entry.key).begin_object();

            if (entry.value.type() == JsonType::Object)
            {
                for (const JsonEntry& member : entry.value)
                {
                    if (!member.key.equals(key))
                    {
                        writer.key(member.key).add_raw(NULL, member.value);
                    }
                }
            }

            write(writer);
            writer.end_object();
        }
    }

    if (!found)
    {
        writer.begin_object(section);
        write(writer);
        writer.end_object();
    }

    writer.end_object();

    if (!adopt(buffer, writer.ok() ? writer.length() : 0))
    {
        trace("config: %s.%s not set, the config would be bigger than %u bytes", section, key, (unsigned int)CONFIG_MAX_JSON);
        return false;
    }

    return true;
}

bool StoredConfig::set_field(const char *section, const char *key, const char *value)
{
    if (!rewrite_field(section, key, [&](JsonWriter& writer) { writer.add_string(key, value); }))
    {
        return false;
    }

    trace("config: %s.%s set to %s", section, key, value);
    return true;
}

bool StoredConfig::set_field(const char *section, const char *key, uint32_t value)
{
    if (!rewrite_field(section, key, [&](JsonWriter& writer) { writer.add_uint(key, value); }))
    {
        return false;
    }

    trace("config: %s.%s set to %u", section, key, (unsigned int)value);
    return true;
}

bool StoredConfig::merge(const JsonView& incoming)
{
    if (!incoming.is_valid() || (incoming.type() != JsonType::Object))
    {
        trace("config: refused, not a JSON object");
        return false;
    }

    uint8_t *buffer = new_buffer();
    if (buffer == NULL)
    {
        return false;
    }

    JsonWriter writer((char *)buffer + HEADER_LEN, BUFFER_CAPACITY - HEADER_LEN);
    writer.begin_object();
    bool ok = ConfigStore::merge_object(json(), incoming, writer, 0);
    writer.end_object();

    if (!adopt(buffer, (ok && writer.ok()) ? writer.length() : 0))
    {
        trace("config: refused, nested too deep or bigger than %u bytes", (unsigned int)CONFIG_MAX_JSON);
        return false;
    }

    return true;
}

bool ConfigStore::update(const char *json, uint32_t len)
{
    StoredConfig stored;
    return stored.merge(JsonView(json, len)) && stored.save();
}

uint32_t ConfigStore::masked(char *out, uint32_t size)
{
    StoredConfig stored;
    const JsonView current = stored.json();

    JsonWriter writer(out, size);
    writer.begin_object();
    bool ok = (current.type() != JsonType::Object) || mask_object(current, writer, 0);
    writer.end_object();

    return (ok && writer.ok()) ? writer.length() : 0;
}
