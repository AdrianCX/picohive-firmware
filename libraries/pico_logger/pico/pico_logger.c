/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"

#include "hardware/exception.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#if !PICO_RP2040
#include "hardware/structs/powman.h"
#endif

#include "pico_logger.h"
#include "log_encryption.h"
#include "core_dump.h"
#include "cm_backtrace/cm_backtrace.h"
#include "os_fs.h"


struct udp_pcb *upstream_pcb = NULL;
struct udp_pcb *binary_upstream_pcb = NULL;

#define ADDRESS_SIZE 46
char address[ADDRESS_SIZE] = {0};

#define APPID_SIZE 64
char app_id[APPID_SIZE] = {0};

// So we can see order and when logs go missing, counter on receiver will skip a few numbers
int log_count = 0;

int32_t max_offline_log_size = 0;
int32_t current_log_size = 0;
os_file_t debug_file = {};

#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

#define MAGIC_CALLSTACK 0xdeadbeef
uint32_t __uninitialized_ram(last_command);
uint32_t __uninitialized_ram(cur_depth);
uint32_t __uninitialized_ram(call_stack_buf)[CMB_CALL_STACK_MAX_DEPTH];

#define MAGIC_RESTART_REASON 0xfeedface
uint32_t __uninitialized_ram(restart_magic);
uint32_t __uninitialized_ram(restart_reason);

static int last_restart_captured = 0;
static restart_reason_t last_restart_reason = RESTART_UNKNOWN;
static void capture_last_restart_reason();

#define WATCHDOG_TICK_US 1000000

uint32_t watchdog_timeout_us = 0;
static volatile uint32_t watchdog_last_update = 0;
static int watchdog_alarm_claimed = 0;
static uint32_t watchdog_clamped_ms = 0;
#define WATCHDOG_SOFT_MAX_MS (60u * 60u * 1000u)

#if PICO_RP2040
// RP2040 watchdog has a max of 8.3s on its timeout. We cannot set it higher.
#define WATCHDOG_MAX_MS (WATCHDOG_LOAD_BITS / 2000)
#else
// RP2040 watchdog has a max of 16.7s on its timeout. We cannot set it higher.
#define WATCHDOG_MAX_MS (WATCHDOG_LOAD_BITS / 1000)
#endif
                                            
#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t __build_id_start[];
extern const uint8_t __build_id_end[];

// Elf32_Nhdr
struct note_hdr {
    uint32_t namesz;   // 4  -> "GNU\0"
    uint32_t descsz;   // 20 for sha1
    uint32_t type;     // 3  == NT_GNU_BUILD_ID
};

int get_build_id(char *data, uint32_t len)
{
    const struct note_hdr *nh = (const struct note_hdr *)__build_id_start;
    
    uint32_t name_padded = (nh->namesz + 3u) & ~3u;
    const uint8_t *desc = __build_id_start + sizeof(*nh) + name_padded;
    size_t source_len = nh->descsz;

    if (source_len*2 > len)
    {
        return 0;
    }
    
    for (uint32_t i=0;i<source_len;i++)
    {
        snprintf(&data[i*2], len-i*2, "%02x", desc[i]);
    }
    
    return source_len*2;
}
    
void set_logging_app_id(const char *id)
{
    strncpy(app_id, id, APPID_SIZE);
    log_encryption_set_device_id(id);
}
    
const char *get_logging_id() {
    return address;
}
    
static void watchdog_tick(void) {
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
    uint32_t now = timer_hw->timerawl;
    timer_hw->alarm[ALARM_NUM] = now + WATCHDOG_TICK_US;

    // Disabled while this tick was already pending
    if (watchdog_timeout_us == 0)
    {
        return;
    }

    if (now - watchdog_last_update < watchdog_timeout_us)
    {
        watchdog_update();
        return;
    }

    core_dump_save_here(RESTART_WATCHDOG);

    // save the callstack
    uint32_t sp = cmb_get_sp();
    uint32_t call_stack_buf[CMB_CALL_STACK_MAX_DEPTH] = {0};
    uint32_t cur_depth = cm_backtrace_call_stack(call_stack_buf, CMB_CALL_STACK_MAX_DEPTH, sp);
    fault_init(&call_stack_buf[0], cur_depth);
    set_restart_reason(RESTART_WATCHDOG);
    
    // restart
    #define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    AIRCR_Register = 0x5FA0004;
}

const char *safestr(const char *value)
{
    return value != NULL ? value : "null";
}

int set_watchdog(uint32_t watchdog_ms)
{
    capture_last_restart_reason();

    if (watchdog_ms != 0)
    {
        if (watchdog_ms > WATCHDOG_SOFT_MAX_MS)
        {
            watchdog_clamped_ms = watchdog_ms;
            watchdog_ms = WATCHDOG_SOFT_MAX_MS;
        }

        watchdog_timeout_us = watchdog_ms * 1000;
        watchdog_last_update = timer_hw->timerawl;

        if (!watchdog_alarm_claimed)
        {
            hardware_alarm_claim(ALARM_NUM);
            watchdog_alarm_claimed = 1;
        }

        // The hardware watchdog is the backup for when the tick IRQ itself cannot run 
        watchdog_enable(WATCHDOG_MAX_MS, 1);

        // Use a regular IRQ so we can save call stack.
        // Highest priority so it still preempts other IRQ handlers that hang, only disabled interrupts can block it.
        hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
        timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + WATCHDOG_TICK_US;
        hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
        irq_set_exclusive_handler(ALARM_IRQ, watchdog_tick);
        irq_set_priority(ALARM_IRQ, PICO_HIGHEST_IRQ_PRIORITY);
        irq_set_enabled(ALARM_IRQ, true);
    }
    else
    {
        // Stop the tick first so it can't feed or re-arm anything, then the hardware watchdog.
        // The alarm stays claimed so set_watchdog() can turn it back on later.
        watchdog_timeout_us = 0;
        hw_clear_bits(&timer_hw->inte, 1u << ALARM_NUM);
        hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
        irq_set_enabled(ALARM_IRQ, false);
        watchdog_disable();
    }
    return 0;
}

int setup_debug_file(int32_t log_size)
{
    if (log_size <= 0)
    {
        if (max_offline_log_size > 0)
        {
            max_offline_log_size = 0;
            os_fs_close(&debug_file);
        }
        os_fs_remove("debug.txt");
        return 1;
    }

    if (max_offline_log_size > 0)
    {
        return 0;
    }
    
    int32_t err = os_fs_open(&debug_file, "debug.txt", OS_FS_WRITE | OS_FS_APPEND | OS_FS_CREATE);
    if (err != 0)
    {
        return 0;
    }
    
    current_log_size = os_fs_size(&debug_file);
    if ((current_log_size < 0) || (current_log_size >= log_size))
    {
        os_fs_close(&debug_file);
        return 0;
    }

    max_offline_log_size = log_size;
    return 1;
}
    
static uint16_t upstream_port = 0;

static void connect_upstream(const ip_addr_t *remote_addr)
{
    const ip_addr_t any_addr = {0};

    upstream_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    binary_upstream_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    
    err_t err = udp_bind(upstream_pcb, &any_addr, 5000);
    if (err != 0)
    {
        trace("Failed binding upstream on local port, error: %d", err);
        return;
    }
    
    udp_connect(upstream_pcb, remote_addr, upstream_port);
    
    err = udp_bind(binary_upstream_pcb, &any_addr, 5001);
    if (err != 0)
    {
        trace("Failed binding upstream on local port, error: %d", err);
        return;
    }
    
    udp_connect(binary_upstream_pcb, remote_addr, upstream_port+1);

    report_saved_crash();
}

static void logging_dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    if (ipaddr == NULL)
    {
        trace("Failed resolving remote logging server address, address: %s", safestr(name));
        return;
    }

    connect_upstream(ipaddr);
}

int start_logging_server(const char *logging_server, uint16_t logging_port, const char *binary_name, const char *hardware_name, const char *software_version, uint32_t watchdog_ms, int access_point)
{ 
    set_watchdog(watchdog_ms);

    cm_backtrace_init(binary_name, hardware_name, software_version);

    if (!access_point)
    {
        strncpy(address, ip4addr_ntoa(netif_ip4_addr(netif_list)), ADDRESS_SIZE-1);
    }

    if (logging_server == NULL || logging_server[0] == 0 || logging_port == 0)
    {
        // intentionally disabled logging
        return 0;
    }
    
    if (!access_point)
    {
        upstream_port = logging_port;

        ip_addr_t remote_addr;
        cyw43_arch_lwip_begin();
        err_t err = dns_gethostbyname(logging_server, &remote_addr, logging_dns_callback, NULL);
        cyw43_arch_lwip_end();

        if (err == ERR_OK)
        {
            connect_upstream(&remote_addr);
        }
        else if (err != ERR_INPROGRESS)
        {
            trace("Failed resolving remote logging server address, address: %s, error: %d", logging_server, err);
            return 0;
        }
    }

    if (watchdog_clamped_ms != 0)
    {
        trace("set_watchdog: timeout %lums exceeds max %lums, using max", (unsigned long)watchdog_clamped_ms, (unsigned long)WATCHDOG_SOFT_MAX_MS);
    }

    return 1;
}

void update_watchdog()
{
    // The tick IRQ feeds the hardware watchdog while this keeps getting called
    watchdog_last_update = timer_hw->timerawl;
}

    
static void send_upstream(const uint8_t *data, uint32_t size)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (p != NULL)
    {
        memcpy(p->payload, data, size);
        udp_send(upstream_pcb, p);
        pbuf_free(p);
    }
}

static void send_message(const char *category, const char *format, va_list args)
{
    uint32_t room = 0;
    char *text = log_encryption_text(&room);

    int written = vsnprintf(text, room + 1, format, args);
    uint32_t len = (written < 0) ? 0 : ((uint32_t)written < room) ? (uint32_t)written : room;

    // One line: the newline is added where a line needs one.
    while ((len > 0) && (text[len - 1] == '\n'))
    {
        len--;
    }
    text[len] = 0;

    char prefix[APPID_SIZE + ADDRESS_SIZE + 8];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%s %s %3d ", app_id, address, log_count);
    prefix_len = (prefix_len < 0) ? 0 : (prefix_len < (int)sizeof(prefix)) ? prefix_len : (int)sizeof(prefix) - 1;
    log_count = (log_count+1)%999;

    if ((max_offline_log_size > 0) && (current_log_size < max_offline_log_size))
    {
        os_fs_write(&debug_file, prefix, prefix_len);
        os_fs_write(&debug_file, text, len);
        os_fs_write(&debug_file, "\n", 1);
        os_fs_sync(&debug_file);
        current_log_size += prefix_len + len + 1;
    }

    if (upstream_pcb && log_encryption_enabled())
    {
        // The packet carries the device id, ip and its own counter, only the line itself goes in.
        const uint8_t *packet = NULL;
        uint32_t packet_size = log_encryption_seal_text(netif_ip4_addr(netif_list)->addr, len, &packet);
        if (packet_size > 0)
        {
            send_upstream(packet, packet_size);
        }
    }
    else if (upstream_pcb)
    {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, prefix_len + len + 1, PBUF_RAM);
        if (p != NULL)
        {
            memcpy(p->payload, prefix, prefix_len);
            memcpy((char *)p->payload + prefix_len, text, len);
            ((char *)p->payload)[prefix_len + len] = '\n';
            udp_send(upstream_pcb, p);
            pbuf_free(p);
        }
    }
}

void trace(const char *format, ...)
{
    if (((max_offline_log_size > 0) && (current_log_size < max_offline_log_size)) || (upstream_pcb))
    {
        va_list args;
        va_start (args, format);
        send_message("trace", format, args);
        va_end (args);
    }
}

void trace_bytes(const uint8_t *buffer, uint32_t size)
{
    if (binary_upstream_pcb)
    {
        struct pbuf *p = pbuf_alloc( PBUF_TRANSPORT, size, PBUF_RAM);
        memcpy(p->payload, buffer, size);
        
        udp_send(binary_upstream_pcb, p);
        pbuf_free(p);
    }
}

void fail(const char *format, ...)
{
    core_dump_save_here(RESTART_FAIL);

    uint32_t sp = cmb_get_sp();
    uint32_t call_stack_buf[CMB_CALL_STACK_MAX_DEPTH] = {0};
    uint32_t cur_depth = cm_backtrace_call_stack(call_stack_buf, CMB_CALL_STACK_MAX_DEPTH, sp);
    fault_init(&call_stack_buf[0], cur_depth);
    set_restart_reason(RESTART_FAIL);
    
    // restart
    #define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    AIRCR_Register = 0x5FA0004;

    // The reset takes a few cycles to land.
    while (1) {}
}

void __attribute__((noreturn)) _exit(int status)
{
    fail("exit(%d)", status);
}

void trace_stack()
{
    cm_backtrace_assert(cmb_get_sp());
}

static void capture_last_restart_reason()
{
    if (last_restart_captured)
    {
        return;
    }
    last_restart_captured = 1;

    if (restart_magic == MAGIC_RESTART_REASON)
    {
        last_restart_reason = (restart_reason_t)restart_reason;
    }
    else if (watchdog_enable_caused_reboot())
    {
        last_restart_reason = RESTART_HW_WATCHDOG;
    }
#if !PICO_RP2040
    else if (powman_hw->last_swcore_pwrup != 0)
    {
        last_restart_reason = RESTART_SLEEP;
    }
#endif
    else
    {
        last_restart_reason = RESTART_UNKNOWN;
    }

    restart_magic = 0;
}

void set_restart_reason(restart_reason_t reason)
{
    capture_last_restart_reason();

    restart_reason = reason;
    restart_magic = MAGIC_RESTART_REASON;
}


restart_reason_t get_last_restart_reason()
{
    capture_last_restart_reason();
    return last_restart_reason;
}

const char *restart_reason_str(restart_reason_t reason)
{
    switch (reason)
    {
        case RESTART_UNKNOWN: return "unknown";
        case RESTART_FAIL: return "fail";
        case RESTART_WATCHDOG: return "watchdog";
        case RESTART_HW_WATCHDOG: return "hw_watchdog";
        case RESTART_FAULT: return "fault";
        case RESTART_REBOOT_COMMAND: return "reboot_command";
        case RESTART_WIFI_INIT_FAILED: return "wifi_init_failed";
        case RESTART_WIFI_CONNECT_FAILED: return "wifi_connect_failed";
        case RESTART_WIFI_LINK_LOST: return "wifi_link_lost";
        case RESTART_APP_START_FAILED: return "app_start_failed";
        case RESTART_UPGRADE: return "upgrade";
        case RESTART_SLEEP: return "sleep";
    }
    return "invalid";
}

void force_restart(restart_reason_t reason)
{
    set_restart_reason(reason);
    update_watchdog();
    watchdog_disable();

    for (int i=0;i<3;i++)
    {
        busy_wait_us(1000000);
    }

    #define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    while (true) { AIRCR_Register = 0x5FA0004; }
}

void fault_init(uint32_t *callstack, uint32_t depth)
{
    // save crash for sending on restart as well.
    cur_depth = depth;
    last_command = MAGIC_CALLSTACK;

    for (int i=0;i<cur_depth;++i)
    {
        call_stack_buf[i] = callstack[i];
    }
}

void clear_saved_crash()
{
    last_command = 0;
}

void report_saved_crash()
{
    if (last_command != MAGIC_CALLSTACK)
    {
        last_command = 0;
        return;
    }

    if (cur_depth > CMB_CALL_STACK_MAX_DEPTH || cur_depth == 0)
    {
        // corrupted ram data
        return;
    }

    trace("report_saved_crash: last boot info, watchdog: %d/%d, last_command: 0x%x, cur_depth: %d", watchdog_caused_reboot(), watchdog_enable_caused_reboot(), last_command, cur_depth);
    cm_print_call_stack(call_stack_buf, cur_depth);
}

int get_crash_callstack(uint32_t **callstack)
{
    if (last_command != MAGIC_CALLSTACK)
    {
        return 0;
    }

    if (cur_depth > CMB_CALL_STACK_MAX_DEPTH || cur_depth == 0)
    {
        // corrupted ram data
        return 0;
    }

    *callstack = call_stack_buf;
    return cur_depth;
}
    
#ifdef __cplusplus
}
#endif
