/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OS_UNIQUE_ID_SIZE 8

#ifndef PICO_BOARD
#define PICO_BOARD "linux"
#endif

// An __uninitialized_ram variable starts zeroed on every run
#define __uninitialized_ram(name) name
#define __not_in_flash_func(name) name
#define __no_inline_not_in_flash_func(name) __attribute__((noinline)) name
#define __time_critical_func(name) name
static inline void tight_loop_contents(void) {}

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef int8_t err_t;

typedef uint32_t ip_addr_t;

#define ERR_OK          0
#define ERR_MEM        -1
#define ERR_BUF        -2
#define ERR_TIMEOUT    -3
#define ERR_RTE        -4
#define ERR_INPROGRESS -5
#define ERR_VAL        -6
#define ERR_WOULDBLOCK -7
#define ERR_USE        -8
#define ERR_ALREADY    -9
#define ERR_ISCONN     -10
#define ERR_CONN       -11
#define ERR_IF         -12
#define ERR_ABRT       -13
#define ERR_RST        -14
#define ERR_CLSD       -15
#define ERR_ARG        -16

#include "lwipopts_common.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *lwip_strerr(err_t err);

typedef uint64_t absolute_time_t;

uint64_t time_us_64(void);
static inline uint32_t time_us_32(void) { return (uint32_t)time_us_64(); }
static inline absolute_time_t get_absolute_time(void) { return time_us_64(); }
static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }

void sleep_us(uint64_t us);
void sleep_ms(uint32_t ms);

uint32_t get_rand_32(void);
uint64_t get_rand_64(void);

// 8 random bytes made on the first start, kept in <data dir>/instance_id. The config is encrypted with a key derived from it.
void os_unique_id(uint8_t id[OS_UNIQUE_ID_SIZE]);

// ---- panic: logs, saves the call stack and restarts (pico_logger's fail())
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

// gpio: no pins on a PC, just mock and log
#define GPIO_OUT 1
#define GPIO_IN 0

void gpio_init(unsigned int gpio);
void gpio_set_dir(unsigned int gpio, bool out);
void gpio_put(unsigned int gpio, bool value);
bool gpio_get(unsigned int gpio);
void gpio_set_pulls(unsigned int gpio, bool up, bool down);
static inline void gpio_pull_up(unsigned int gpio) { gpio_set_pulls(gpio, true, false); }
static inline void gpio_pull_down(unsigned int gpio) { gpio_set_pulls(gpio, false, true); }
static inline void gpio_disable_pulls(unsigned int gpio) { gpio_set_pulls(gpio, false, false); }

#define GPIO_FUNC_UART 2
#define GPIO_FUNC_PWM 4
#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_NULL 0x1f

#define GPIO_DRIVE_STRENGTH_2MA 0
#define GPIO_DRIVE_STRENGTH_4MA 1
#define GPIO_DRIVE_STRENGTH_8MA 2
#define GPIO_DRIVE_STRENGTH_12MA 3

static inline void gpio_set_function(unsigned int gpio, unsigned int fn) { (void)gpio; (void)fn; }
static inline void gpio_set_drive_strength(unsigned int gpio, unsigned int drive) { (void)gpio; (void)drive; }

#define GPIO_IRQ_LEVEL_LOW 0x1u
#define GPIO_IRQ_LEVEL_HIGH 0x2u
#define GPIO_IRQ_EDGE_FALL 0x4u
#define GPIO_IRQ_EDGE_RISE 0x8u

typedef void (*gpio_irq_callback_t)(unsigned int gpio, uint32_t event_mask);
static inline void gpio_set_irq_enabled(unsigned int gpio, uint32_t event_mask, bool enabled) {}
static inline void gpio_set_irq_enabled_with_callback(unsigned int gpio, uint32_t event_mask, bool enabled,
                                                      gpio_irq_callback_t callback) {}

#define PWM_CHAN_A 0
#define PWM_CHAN_B 1

static inline unsigned int pwm_gpio_to_slice_num(unsigned int gpio) { return (gpio >> 1) & 7; }
static inline unsigned int pwm_gpio_to_channel(unsigned int gpio) { return gpio & 1; }

void pwm_set_chan_level(unsigned int slice, unsigned int channel, uint16_t level);
static inline void pwm_set_gpio_level(unsigned int gpio, uint16_t level) { pwm_set_chan_level(pwm_gpio_to_slice_num(gpio), pwm_gpio_to_channel(gpio), level); }
static inline void pwm_set_wrap(unsigned int slice, uint16_t wrap) { (void)slice; (void)wrap; }
static inline void pwm_set_clkdiv(unsigned int slice, float divider) { (void)slice; (void)divider; }
static inline void pwm_set_phase_correct(unsigned int slice, bool phase_correct) { (void)slice; (void)phase_correct; }
static inline void pwm_set_enabled(unsigned int slice, bool enabled) { (void)slice; (void)enabled; }

// ---- clocks: the rate the board's code divides down from, an rp2040's default system clock.
#define clk_sys 5
static inline uint32_t clock_get_hz(unsigned int clock) { (void)clock; return 125000000; }

#ifdef __cplusplus
}
#endif
