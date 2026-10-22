/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>
#include <time.h>

#include "common_functions.h"
#include "memory_stats.h"
#include "pico_os_version.h"
#include "os_fs.h"
#include "os_loop.h"
#include "os_platform.h"
#include "os_linux.h"
#include "pico_logger.h"
#include "session.h"

#include "app_config.h"
#include "config_store.h"

#include "command_mqtt_manager.h"

AppConfig app = {};
CommandMqttManager *mqtt_manager = NULL;

AppConfig& app_info()
{
	return app;
}

#ifndef PICO_OS_LINUX_STACK_SIZE
#define PICO_OS_LINUX_STACK_SIZE (256 * 1024)
#endif

static int run(void *);

int main(int argc, char **argv)
{
    os_platform_init(argc, argv);

    return os_run_on_stack(PICO_OS_LINUX_STACK_SIZE, run, NULL);
}

static int run(void *)
{
    memory_stats_init();

    set_watchdog(0);

    os_fs_init();

    read_config();

    if (get_debug_config().offline_log_size > 0)
    {
        setup_debug_file(get_debug_config().offline_log_size);
    }

    get_app_info(&app);

    if (app.version == NULL)
    {
        app.version = PICO_OS_APP_VERSION;
    }

    if (app.on_config_update != NULL)
    {
        StoredConfig stored;
        app.on_config_update(stored.json());
    }

    app.on_boot();

    bool unconfigured = app.access_point();

    if (unconfigured)
    {
        trace("unconfigured: no MQTT and no logging server until the config has them");
    }

    start_logging_server(get_config().mqtt_host, get_config().logging_port, app.name, PICO_BOARD, app.version, app.watchdog_timeout, unconfigured);

    trace("Starting, data in %s", os_data_dir());

    if ((get_config().mqtt_host[0] != 0) && (!unconfigured))
    {
        mqtt_manager = &CommandMqttManager::get();
    }

    if (!app.on_wifi_ready())
    {
        trace("on_startup returned error");
        force_restart(RESTART_APP_START_FAILED);
        return 1;
    }

    uint64_t start_us = to_us_since_boot(get_absolute_time());

    bool report_crash = true;

    while (!os_exit_requested()) {
        OsLoop::run_once(os_loop_ms());
        update_watchdog();

        uint64_t now_us = to_us_since_boot(get_absolute_time());

        if ((now_us - start_us > 30000000LL) || report_crash)
        {
            report_crash = false;

            start_us = now_us;

            report_saved_crash();

            char buildid[256] = {};
            get_build_id(buildid, sizeof(buildid));

            uint32_t free_heap = getFreeHeap();

            trace("app[%s] version[%s] os[%s] buildid[%s] Heap[%lu/%lu] Free[%lu] MinFree[%lu] LargestFree[%lu] Stack[%lu/%lu] lastRestartReason[%s]\n", app.name, app.version, PICO_OS_OS_VERSION, buildid, getUsedHeap(), getTotalHeap(), free_heap, getMinFreeHeap(), getLargestFreeBlock(), getMaxUsedStack(), getTotalStack(), restart_reason_str(get_last_restart_reason()));
        }

        if (mqtt_manager != NULL)
        {
            mqtt_manager->update(now_us);
        }

        app.on_update(now_us);
    }

    trace("exiting");
    set_watchdog(0);
    return os_exit_code();
}
