/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "config_store.h"

static const uint32_t MAX_DEPTH = 8;

bool ConfigStore::is_secret(const JsonView& key)
{
    return key.equals("pass") || key.equals("key") || key.equals("token") || key.starts_with("secret");
}

static bool is_empty_string(const JsonView& value)
{
    return (value.type() == JsonType::String) && (value.length() == 2);
}

bool ConfigStore::merge_object(const JsonView& stored, const JsonView& incoming, JsonWriter& out, uint32_t depth)
{
    if (depth > MAX_DEPTH)
    {
        return false;
    }

    for (const JsonEntry& entry : incoming)
    {
        JsonType type = entry.value.type();

        if (ConfigStore::is_secret(entry.key))
        {
            if (type == JsonType::Null)
            {
                out.key(entry.key).add_string(NULL, "");
            }
            else if ((type == JsonType::String) && !is_empty_string(entry.value))
            {
                out.key(entry.key).add_raw(NULL, entry.value);
            }
            else
            {
                JsonView kept = stored.member(entry.key);
                if (kept.type() == JsonType::String)
                {
                    out.key(entry.key).add_raw(NULL, kept);
                }
                else
                {
                    out.key(entry.key).add_string(NULL, "");
                }
            }
        }
        else if (type == JsonType::Object)
        {
            out.key(entry.key).begin_object();
            if (!merge_object(stored.member(entry.key), entry.value, out, depth + 1))
            {
                return false;
            }
            out.end_object();
        }
        else
        {
            out.key(entry.key).add_raw(NULL, entry.value);
        }
    }

    return true;
}

bool ConfigStore::mask_object(const JsonView& stored, JsonWriter& out, uint32_t depth)
{
    if (depth > MAX_DEPTH)
    {
        return false;
    }

    for (const JsonEntry& entry : stored)
    {
        if (ConfigStore::is_secret(entry.key))
        {
            out.key(entry.key).add_bool(NULL, (entry.value.type() == JsonType::String) && !is_empty_string(entry.value));
        }
        else if (entry.value.type() == JsonType::Object)
        {
            out.key(entry.key).begin_object();
            if (!mask_object(entry.value, out, depth + 1))
            {
                return false;
            }
            out.end_object();
        }
        else
        {
            out.key(entry.key).add_raw(NULL, entry.value);
        }
    }

    return true;
}
