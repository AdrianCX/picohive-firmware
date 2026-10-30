/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <new>
#include <time.h>

#include "os_fs.h"
#include "os_platform.h"

#include "pico_logger.h"
#include "app_config.h"
#include "config_store.h"

Config config = {};
DebugConfig debug_config = {};

bool read_static_file(const char *file_name, uint8_t *address, uint32_t size)
{
    os_file_t file = {};
    
    if (os_fs_open(&file, file_name, OS_FS_READ | OS_FS_WRITE | OS_FS_CREATE) != 0)
    {
        return false;
    }

    os_fs_read(&file, address, size);
    os_fs_close(&file);
    return true;
}

bool write_static_file(const char *file_name, uint8_t *address, uint32_t size)
{
    os_file_t file = {};

    int error = os_fs_open(&file, file_name, OS_FS_READ | OS_FS_WRITE | OS_FS_CREATE);
    if (error != 0)
    {
        trace("write_static_file: open failed, file[%s] error[%d]", file_name, error);
        return false;
    }

    int writeCode = os_fs_write(&file, address, size);
    if (writeCode < 0)
    {
        trace("write_static_file: write failed, file[%s] error[%d]", file_name, writeCode);
        os_fs_close(&file);
        return false;
    }
    
    os_fs_close(&file);
    return true;
}


static void apply_logging_config()
{
    set_logging_app_id(config.mqtt_user);
    if (!set_logging_key(config.log_key))
    {
        trace("config: log key is not 32 or 64 hex characters, logging stays unencrypted");
    }
}

bool read_config()
{
    StoredConfig stored;

    if (stored.loaded())
    {
        trace("config: loaded, %u bytes", (unsigned int)stored.json().length());
    }

    apply_config(stored.json());
    return stored.loaded();
}

void apply_config(const JsonView& json)
{
    uint32_t number = 0;

    new (&config) Config();

    json.get_string("$.wifi.ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    json.get_string("$.wifi.pass", config.wifi_pass, sizeof(config.wifi_pass));
    if (json.get_uint("$.wifi.auth", number) && (number < WIFI_AUTH_COUNT))
    {
        config.wifi_auth = number;
    }
    json.get_string("$.wifi.country", config.wifi_country, sizeof(config.wifi_country));

    json.get_string("$.web.user", config.web_user, sizeof(config.web_user));
    json.get_string("$.web.pass", config.web_pass, sizeof(config.web_pass));

    json.get_string("$.mqtt.host", config.mqtt_host, sizeof(config.mqtt_host));
    json.get_string("$.mqtt.user", config.mqtt_user, sizeof(config.mqtt_user));
    json.get_string("$.mqtt.pass", config.mqtt_pass, sizeof(config.mqtt_pass));
    json.get_bool("$.mqtt.tls", config.mqtt_tls);
    if (json.get_uint("$.mqtt.port", number) && (number <= 0xffff))
    {
        config.mqtt_port = number;
    }

    if (json.get_uint("$.log.port", number) && (number <= 0xffff))
    {
        config.logging_port = number;
    }
    json.get_string("$.log.key", config.log_key, sizeof(config.log_key));

    json.get_bool("$.mcp.enabled", config.mcp_enabled);

    debug_config = DebugConfig();
    json.get_uint("$.debug.log_size", debug_config.offline_log_size);

    trace("config: ssid=%s auth=%u country=%s web_user=%s host=%s user=%s tls=%d port=%u logport=%u offline_log=%u mcp=%d", config.wifi_ssid, (unsigned int)config.wifi_auth, config.wifi_country, config.web_user, config.mqtt_host, config.mqtt_user, config.mqtt_tls, config.mqtt_port, config.logging_port, (unsigned int)debug_config.offline_log_size, config.mcp_enabled);

    apply_logging_config();
}

Config& get_config()
{
    return config;
}

DebugConfig& get_debug_config()
{
    return debug_config;
}

