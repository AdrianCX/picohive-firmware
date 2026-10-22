/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _COMMON_FUNCTIONS_H
#define _COMMON_FUNCTIONS_H

#include <stdint.h>
#include <stdio.h>

#include "memory_stats.h"
#include "os_platform.h"

class JsonView;

#ifdef __cplusplus
extern "C" {
#endif

struct AppConfig
{
    void (*on_boot)();
    bool (*access_point)();
    void (*on_wifi_failed)();
    bool (*on_wifi_ready)();
    void (*on_update)(uint64_t now_us);
    void (*on_config_update)(const JsonView& config);

    const char* name;
    const char* version;
    uint32_t watchdog_timeout;
};

AppConfig& app_info();
	
void get_app_info(AppConfig *config);

	
#ifdef __cplusplus
}
#endif


#endif
