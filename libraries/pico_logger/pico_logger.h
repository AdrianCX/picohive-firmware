/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _PICO_LOGGER_H
#define _PICO_LOGGER_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RESTART_UNKNOWN = 0,            // power on, reset pin, or a reset nobody recorded
    RESTART_FAIL = 1,               // fail(): lwip assert, exit(), panic()
    RESTART_WATCHDOG = 2,           // update_watchdog() not called within watchdog_ms
    RESTART_HW_WATCHDOG = 3,        // hardware watchdog, the watchdog tick itself could not run
    RESTART_FAULT = 4,              // hard fault
    RESTART_REBOOT_COMMAND = 5,     // reboot command
    RESTART_WIFI_INIT_FAILED = 6,   // cyw43_arch_init failed
    RESTART_WIFI_CONNECT_FAILED = 7,// could not join the wifi network
    RESTART_WIFI_LINK_LOST = 8,     // wifi link dropped
    RESTART_APP_START_FAILED = 9,   // on_wifi_ready() returned false
    RESTART_UPGRADE = 10,           // new firmware written
    RESTART_SLEEP = 11,             // woke up from pico_sleep deep sleep
} restart_reason_t;

// Initialize logging library. Provided parameters must be statically allocated.
int set_watchdog(uint32_t watchdog_ms);
int start_logging_server(const char *logging_server, uint16_t logging_port, const char *binary_name, const char *hardware_name, const char *software_version, uint32_t watchdog_ms, int access_point);
void set_logging_app_id(const char *id);

// AES-GCM key for the log lines sent upstream, 32 hex characters (AES-128) or 64 (AES-256), empty sends plain text.
// Returns 0 for a key that doesn't parse, logs then go out as plain text.
int set_logging_key(const char *hex);
void update_watchdog();
void trace(const char *parameters, ...);
void trace_bytes(const uint8_t *buffer, uint32_t size);
void fail(const char *parameters, ...) __attribute__((noreturn));
void trace_stack();
void fault_init(uint32_t *callstack, uint32_t depth);
void force_restart(restart_reason_t reason);
void set_restart_reason(restart_reason_t reason);
restart_reason_t get_last_restart_reason();
const char *restart_reason_str(restart_reason_t reason);
void report_saved_crash();
void clear_saved_crash();
const char *safestr(const char *value);

const char *get_logging_id();
int get_build_id(char *data, uint32_t len);
int get_crash_callstack(uint32_t **callstack);

int setup_debug_file(int32_t log_size);
#ifdef __cplusplus
}
#endif


#endif
