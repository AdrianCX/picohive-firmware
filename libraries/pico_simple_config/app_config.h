/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _APP_CONFIG_H
#define _APP_CONFIG_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONFIG_STR 64
#define MAX_URL_STR 256

#define WIFI_AUTH_COUNT 6
#define WIFI_AUTH_DEFAULT 2

struct Config
{
    char wifi_ssid[MAX_CONFIG_STR] = {};
    char wifi_pass[MAX_CONFIG_STR] = {};
    char mqtt_host[MAX_CONFIG_STR] = {};
    char mqtt_user[MAX_CONFIG_STR] = {};
    char mqtt_pass[MAX_CONFIG_STR] = {};
    bool mqtt_tls = false;
    uint16_t mqtt_port = 1883;
    uint16_t logging_port = 23500;
    uint32_t wifi_auth = WIFI_AUTH_DEFAULT;
    char wifi_country[3] = "NL";
    char web_user[MAX_CONFIG_STR] = {};
    char web_pass[MAX_CONFIG_STR] = {};
    char log_key[MAX_CONFIG_STR + 1] = {};
    bool mcp_enabled = false;
};

struct DebugConfig
{
    uint32_t offline_log_size = 0;
};

bool read_config();

bool read_static_file(const char *file_name, uint8_t *address, uint32_t size);
bool write_static_file(const char *file_name, uint8_t *address, uint32_t size);

Config& get_config();

DebugConfig& get_debug_config();

#ifdef __cplusplus
}

#include "pico_mjson.h"

void apply_config(const JsonView& json);
#endif

#endif
