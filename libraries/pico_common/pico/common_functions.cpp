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
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "pico/cyw43_arch.h"
#include "pico/platform.h"
#include "pico/stdio.h"
#include "pico_logger.h"
#include "session.h"
#include "wifi.h"
#include "upgrade_helper.h"
#include "flash_layout.h"

#include "os_fs.h"
#include "app_config.h"
#include "wifi_settings.h"
#include "config_store.h"

#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"

#include "command_mqtt_manager.h"

// The linker checks the firmware fits its slot (build_id.ld).
#define PICO_OS_STR_(x) #x
#define PICO_OS_STR(x) PICO_OS_STR_(x)
asm(".global __pico_os_slot_size\n.set __pico_os_slot_size, " PICO_OS_STR(PICO_OS_SLOT_SIZE));

AppConfig app = {};
CommandMqttManager *mqtt_manager = NULL;

AppConfig& app_info()
{
	return app;
}

int main()
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
    
    if (cyw43_arch_init_with_country(get_wifi_country()))
    {
        force_restart(RESTART_WIFI_INIT_FAILED);
        return 1;
    }    

    bool access_point = app.access_point();
    
    if (!access_point)
    {
        cyw43_arch_enable_sta_mode();
        cyw43_arch_disable_ap_mode();

        // One AP per device, nothing to roam to: the "roam_off" iovar, set to 1.
        uint8_t roam_off[] = {'r', 'o', 'a', 'm', '_', 'o', 'f', 'f', 0, 1, 0, 0, 0};
        int roam_err = cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, sizeof(roam_off), roam_off, CYW43_ITF_STA);

        if (roam_err != 0)
        {
            trace("roam_off failed: %d", roam_err);
        }

        int err_code = cyw43_arch_wifi_connect_timeout_ms(get_config().wifi_ssid, get_config().wifi_pass, get_wifi_auth_type(), 30000);
        
        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

        if (err_code != 0)
        {
            for (int i=0;i<5;++i)
            {
                sleep_ms(1000);
            }

            app.on_wifi_failed();

            force_restart(RESTART_WIFI_CONNECT_FAILED);
            return 1;
        }
    }
    else
    {
        cyw43_arch_enable_ap_mode(WIFI_AP_SSID, WIFI_AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

        ip4_addr_t gw;
        ip4_addr_t mask;
        gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
        mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

        dhcp_server_init((dhcp_server_t *) calloc(1, sizeof(dhcp_server_t)), &gw, &mask);
        dns_server_init((dns_server_t *) calloc(1, sizeof(dns_server_t)), &gw);
    }

    start_logging_server(get_config().mqtt_host, get_config().logging_port, app.name, PICO_BOARD, app.version, app.watchdog_timeout, access_point);
    
    trace("Starting");

    if ((get_config().mqtt_host[0] != 0) && (!access_point))
    {
        mqtt_manager = &CommandMqttManager::get();
    }
    
    if (!app.on_wifi_ready())
    {
        for (int i=0;i<5;++i)
        {
            trace("on_startup returned error");
            sleep_ms(1000);
        }

        force_restart(RESTART_APP_START_FAILED);
        return 1;
    }

    uint64_t start_us = to_us_since_boot(get_absolute_time());

    bool report_crash = true;

    while (true) {
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        sys_check_timeouts();
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

        if (!access_point)
        {
            int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        
            if (status != CYW43_LINK_UP)
            {
                sleep_ms(1000);

                force_restart(RESTART_WIFI_LINK_LOST);
            }
        }

        cyw43_arch_lwip_end();
    }

    cyw43_arch_deinit();
    return 0;
}
