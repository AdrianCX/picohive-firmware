/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void os_platform_init(int argc, char **argv);
const char *os_data_dir(void);
const char *os_exe_path(void);
uint16_t os_map_port(uint16_t port);
int os_loop_ms(void);

bool os_trace_stdout(void);
bool os_abort_on_fail(void);
bool os_watchdog_disabled(void);

bool os_exit_on_restart(void);
void os_restart(void) __attribute__((noreturn));
	
void os_restart_from_signal(void) __attribute__((noreturn));

int os_run_on_stack(uint32_t size, int (*fn)(void *), void *arg);
void os_stack_region(uint32_t **bottom, uint32_t *size);
void os_resolve(const char *host, void (*done)(int err, uint32_t addr, void *context), void *context);

bool os_exit_requested(void);
void os_request_exit(int code);
int os_exit_code(void);

#ifdef __cplusplus
}
#endif
