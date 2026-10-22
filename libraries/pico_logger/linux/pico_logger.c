/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <link.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "os_fs.h"
#include "os_platform.h"
#include "os_linux.h"
#include "pico_logger.h"
#include "log_encryption.h"

#define CALL_STACK_MAX_DEPTH 50

static int upstream_fd = -1;
static int binary_upstream_fd = -1;

#define BUFFER_SIZE 1500
static char buffer[BUFFER_SIZE] = {0};

#define ADDRESS_SIZE 46
static char address[ADDRESS_SIZE] = {0};
static uint32_t address_ip = 0;

#define APPID_SIZE 64
static char app_id[APPID_SIZE] = {0};

static int log_count = 0;

static int32_t max_offline_log_size = 0;
static int32_t current_log_size = 0;
static os_file_t debug_file = {0};

static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;

#define MAGIC_CALLSTACK 0xdeadbeef
#define MAGIC_RESTART_REASON 0xfeedface

struct restart_state
{
    uint32_t last_command;
    uint32_t cur_depth;
    uint32_t call_stack_buf[CALL_STACK_MAX_DEPTH];
    uint32_t restart_magic;
    uint32_t restart_reason;
};

static struct restart_state saved = {0};
static int saved_fd = -1;

static int last_restart_captured = 0;
static restart_reason_t last_restart_reason = RESTART_UNKNOWN;

static void saved_open(void)
{
    if (saved_fd >= 0)
    {
        return;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/restart.bin", os_data_dir());
    saved_fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if ((saved_fd < 0) || (pread(saved_fd, &saved, sizeof(saved), 0) != (ssize_t)sizeof(saved)))
    {
        memset(&saved, 0, sizeof(saved));
    }
}

static void saved_write(void)
{
    if (saved_fd >= 0)
    {
        ssize_t ignored = pwrite(saved_fd, &saved, sizeof(saved), 0);
        (void)ignored;
    }
}

static uintptr_t exe_base = 0;

__attribute__((constructor)) static void call_stack_prepare(void)
{
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)&call_stack_prepare, &info) != 0)
    {
        exe_base = (uintptr_t)info.dli_fbase;
    }

    void *frames[4];
    backtrace(frames, 4);
}

static uint32_t call_stack(uint32_t *out, uint32_t max, int skip)
{
    void *frames[CALL_STACK_MAX_DEPTH + 8];
    int count = backtrace(frames, CALL_STACK_MAX_DEPTH + 8);
    uint32_t depth = 0;

    for (int i = 1 + skip; (i < count) && (depth < max); ++i)
    {
        uintptr_t addr = (uintptr_t)frames[i];

        if ((addr > exe_base) && (addr - exe_base < 0xffffffffu))
        {
            out[depth++] = (uint32_t)(addr - exe_base - 1);
        }
    }
    return depth;
}

static void print_call_stack(const char *what, const uint32_t *stack, uint32_t depth)
{
    char line[CALL_STACK_MAX_DEPTH * 9 + 1] = {0};
    int at = 0;

    for (uint32_t i = 0; i < depth; ++i)
    {
        at += snprintf(&line[at], sizeof(line) - at, "%08lx ", (unsigned long)stack[i]);
    }

    trace("%s: Print call stack: addr2line -e %s -afpiC %s", what, os_exe_path(), line);
}

static void write_stderr(const char *text)
{
    ssize_t ignored = write(STDERR_FILENO, text, strlen(text));
    (void)ignored;
}

static _Atomic uint64_t watchdog_timeout_us = 0;
static _Atomic uint64_t watchdog_last_update = 0;
static uint32_t watchdog_clamped_ms = 0;
static pthread_t loop_thread;
static pthread_t watchdog;
static int watchdog_started = 0;
static pthread_mutex_t watchdog_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t watchdog_wake = PTHREAD_COND_INITIALIZER;
static int watchdog_stop = 0;
#define WATCHDOG_SOFT_MAX_MS (60u * 60u * 1000u)

static void on_watchdog_signal(int sig)
{
    (void)sig;

    uint32_t stack[CALL_STACK_MAX_DEPTH];
    uint32_t depth = call_stack(stack, CALL_STACK_MAX_DEPTH, 1);
    fault_init(stack, depth);
    set_restart_reason(RESTART_WATCHDOG);

    write_stderr("watchdog: update_watchdog() not called within the timeout, the loop is stuck, restarting\n");
    os_restart_from_signal();
}

static void *watchdog_thread(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&watchdog_lock);
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 1;
        while (!watchdog_stop && (pthread_cond_timedwait(&watchdog_wake, &watchdog_lock, &until) == 0))
        {
        }
        int stop = watchdog_stop;
        pthread_mutex_unlock(&watchdog_lock);

        if (stop)
        {
            return NULL;
        }

        uint64_t timeout = atomic_load(&watchdog_timeout_us);
        if ((timeout != 0) && (time_us_64() - atomic_load(&watchdog_last_update) >= timeout))
        {
            pthread_kill(loop_thread, SIGUSR2);
            sleep_ms(5000);
            set_restart_reason(RESTART_HW_WATCHDOG);
            os_restart_from_signal();
        }
    }
    return NULL;
}

static void watchdog_join(void)
{
    pthread_mutex_lock(&watchdog_lock);
    watchdog_stop = 1;
    pthread_cond_signal(&watchdog_wake);
    pthread_mutex_unlock(&watchdog_lock);

    pthread_join(watchdog, NULL);
}

static void on_fault(int sig)
{
    uint32_t stack[CALL_STACK_MAX_DEPTH];
    uint32_t depth = call_stack(stack, CALL_STACK_MAX_DEPTH, 1);
    fault_init(stack, depth);
    set_restart_reason((sig == SIGABRT) ? RESTART_FAIL : RESTART_FAULT);

    write_stderr("fault: ");
    write_stderr(strsignal(sig));
    write_stderr(", call stack saved, the next start reports it\n");

    void *frames[CALL_STACK_MAX_DEPTH];
    backtrace_symbols_fd(frames, backtrace(frames, CALL_STACK_MAX_DEPTH), STDERR_FILENO);

    raise(sig);
}

static void install_handlers(void)
{
    static int installed = 0;
    if (installed)
    {
        return;
    }
    installed = 1;

    loop_thread = pthread_self();

    // A stack overflow still gets its report.
    static uint8_t alt_stack[64 * 1024];
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fault;
    sa.sa_flags = SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
    {
        sigaction(signals[i], &sa, NULL);
    }

    struct sigaction wd;
    memset(&wd, 0, sizeof(wd));
    wd.sa_handler = on_watchdog_signal;
    sigemptyset(&wd.sa_mask);
    sigaction(SIGUSR2, &wd, NULL);
}

// ---- the local address, what the board's netif has

static void find_local_address(void)
{
    struct ifaddrs *list = NULL;
    strncpy(address, "127.0.0.1", ADDRESS_SIZE - 1);
    address_ip = htonl(INADDR_LOOPBACK);

    if (getifaddrs(&list) != 0)
    {
        return;
    }

    for (struct ifaddrs *it = list; it != NULL; it = it->ifa_next)
    {
        if ((it->ifa_addr == NULL) || (it->ifa_addr->sa_family != AF_INET) || (it->ifa_flags & IFF_LOOPBACK) || !(it->ifa_flags & IFF_UP))
        {
            continue;
        }

        struct sockaddr_in *in = (struct sockaddr_in *)it->ifa_addr;
        address_ip = in->sin_addr.s_addr;
        inet_ntop(AF_INET, &in->sin_addr, address, ADDRESS_SIZE);
        break;
    }

    freeifaddrs(list);
}

static int open_upstream(const struct sockaddr_in *remote, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        return -1;
    }

    struct sockaddr_in target = *remote;
    target.sin_port = htons(port);

    if (connect(fd, (struct sockaddr *)&target, sizeof(target)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void send_upstream(const uint8_t *data, uint32_t size)
{
    ssize_t ignored = send(upstream_fd, data, size, MSG_DONTWAIT);
    (void)ignored;
}

static void print_stdout(const char *line, int len)
{
    uint64_t now = time_us_64();
    while ((len > 0) && ((line[len - 1] == '\n') || (line[len - 1] == '\r') || (line[len - 1] == 0)))
    {
        len--;
    }
    printf("[%5llu.%06llu] %.*s\n", (unsigned long long)(now / 1000000), (unsigned long long)(now % 1000000), len, line);
}

static void send_message(const char *format, va_list args)
{
    int size = snprintf(buffer, BUFFER_SIZE, "%s %s %3d ", app_id, address, log_count);
    int prefix = size;

    if (size < BUFFER_SIZE-1)
    {
        log_count = (log_count+1)%999;

        size += vsnprintf(&buffer[size], BUFFER_SIZE-size-1, format, args);

        if (size < BUFFER_SIZE-1)
        {
            if (buffer[size-1] != '\n')
            {
                buffer[size] = '\n';
                buffer[++size] = 0;
            }
        }

        if (size > BUFFER_SIZE)
        {
            buffer[BUFFER_SIZE-1] = 0;
            size = BUFFER_SIZE;
        }
    }

    if (os_trace_stdout())
    {
        print_stdout(&buffer[prefix], size - prefix);
    }

    if ((max_offline_log_size > 0) && (current_log_size < max_offline_log_size))
    {
        os_fs_write(&debug_file, &buffer[0], size);
        os_fs_sync(&debug_file);
        current_log_size += size;
    }

    if ((upstream_fd >= 0) && log_encryption_enabled())
    {
        // The packet carries the device id, ip and its own counter, only the line itself goes in.
        int len = size - prefix;
        while (len > 0 && (buffer[prefix + len - 1] == '\n' || buffer[prefix + len - 1] == 0))
        {
            len--;
        }

        const uint8_t *packet = NULL;
        uint32_t packet_size = (len >= 0) ? log_encryption_seal(address_ip, &buffer[prefix], len, &packet) : 0;
        if (packet_size > 0)
        {
            send_upstream(packet, packet_size);
        }
    }
    else if (upstream_fd >= 0)
    {
        send_upstream((const uint8_t *)buffer, size);
    }
}

static void capture_last_restart_reason(void)
{
    if (last_restart_captured)
    {
        return;
    }
    last_restart_captured = 1;

    saved_open();

    if (saved.restart_magic == MAGIC_RESTART_REASON)
    {
        last_restart_reason = (restart_reason_t)saved.restart_reason;
    }
    else
    {
        last_restart_reason = RESTART_UNKNOWN;
    }

    saved.restart_magic = 0;
    saved_write();
}

// The executable's GNU build id.
struct build_id_search
{
    const uint8_t *desc;
    uint32_t size;
};

static int find_build_id(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void)size;
    struct build_id_search *found = (struct build_id_search *)arg;

    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (phdr->p_type != PT_NOTE)
        {
            continue;
        }

        const uint8_t *at = (const uint8_t *)(info->dlpi_addr + phdr->p_vaddr);
        const uint8_t *end = at + phdr->p_memsz;

        while (at + sizeof(ElfW(Nhdr)) <= end)
        {
            const ElfW(Nhdr) *note = (const ElfW(Nhdr) *)at;
            uint32_t name_padded = (note->n_namesz + 3u) & ~3u;
            uint32_t desc_padded = (note->n_descsz + 3u) & ~3u;
            const uint8_t *desc = at + sizeof(*note) + name_padded;

            if ((note->n_type == NT_GNU_BUILD_ID) && (note->n_namesz == 4) && (memcmp(at + sizeof(*note), "GNU", 4) == 0))
            {
                found->desc = desc;
                found->size = note->n_descsz;
                return 1;
            }
            at = desc + desc_padded;
        }
    }

    // The executable is the first object reported: stop there.
    return 1;
}

int get_build_id(char *data, uint32_t len)
{
    struct build_id_search search = {NULL, 0};
    dl_iterate_phdr(find_build_id, &search);

    if ((search.desc == NULL) || (search.size * 2 >= len))
    {
        return 0;
    }

    for (uint32_t i = 0; i < search.size; i++)
    {
        snprintf(&data[i*2], len-i*2, "%02x", search.desc[i]);
    }

    return search.size*2;
}

void set_logging_app_id(const char *id)
{
    strncpy(app_id, id, APPID_SIZE - 1);
    log_encryption_set_device_id(id);
}

const char *get_logging_id() {
    return address;
}

const char *safestr(const char *value)
{
    return value != NULL ? value : "null";
}

int set_watchdog(uint32_t watchdog_ms)
{
    install_handlers();

    if ((watchdog_ms != 0) && os_watchdog_disabled())
    {
        trace("set_watchdog: %lums asked for, off (--no-watchdog)", (unsigned long)watchdog_ms);
        watchdog_ms = 0;
    }

    if (watchdog_ms > WATCHDOG_SOFT_MAX_MS)
    {
        watchdog_clamped_ms = watchdog_ms;
        watchdog_ms = WATCHDOG_SOFT_MAX_MS;
    }

    atomic_store(&watchdog_last_update, time_us_64());
    atomic_store(&watchdog_timeout_us, (uint64_t)watchdog_ms * 1000);

    if ((watchdog_ms != 0) && !watchdog_started)
    {
        watchdog_started = 1;

        if (pthread_create(&watchdog, NULL, watchdog_thread, NULL) == 0)
        {
            atexit(watchdog_join);
        }
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
    if (current_log_size < 0)
    {
        return 0;
    }

    if (current_log_size >= log_size)
    {
        return 0;
    }

    max_offline_log_size = log_size;
    return 1;
}

static uint16_t upstream_port = 0;

// On the loop thread, once the resolver thread has the logging server's address.
static void logging_server_resolved(int err, uint32_t addr, void *context)
{
    const char *logging_server = (const char *)context;

    if (err != 0)
    {
        trace("Failed resolving remote logging server address, address: %s", safestr(logging_server));
        return;
    }

    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = addr;

    upstream_fd = open_upstream(&remote, upstream_port);
    binary_upstream_fd = open_upstream(&remote, upstream_port + 1);

    if (upstream_fd < 0)
    {
        trace("Failed opening the logging socket to %s:%d: %s", safestr(logging_server), (int)upstream_port, strerror(errno));
        return;
    }

    report_saved_crash();
}

int start_logging_server(const char *logging_server, uint16_t logging_port, const char *binary_name, const char *hardware_name, const char *software_version, uint32_t watchdog_ms, int access_point)
{
    (void)binary_name;
    (void)hardware_name;
    (void)software_version;

    set_watchdog(watchdog_ms);

    find_local_address();

    if (watchdog_clamped_ms != 0)
    {
        trace("set_watchdog: timeout %lums exceeds max %lums, using max", (unsigned long)watchdog_clamped_ms, (unsigned long)WATCHDOG_SOFT_MAX_MS);
    }

    if (logging_server == NULL || logging_server[0] == 0 || logging_port == 0)
    {
        // intentionally disabled logging
        return 0;
    }

    if (!access_point)
    {
        upstream_port = logging_port;

        // As the board's dns callback: connect once the address is known.
        os_resolve(logging_server, logging_server_resolved, (void *)logging_server);
    }

    return 1;
}

void update_watchdog()
{
    atomic_store(&watchdog_last_update, time_us_64());
}

void trace(const char *format, ...)
{
    if (!os_trace_stdout() && (upstream_fd < 0) && ((max_offline_log_size <= 0) || (current_log_size >= max_offline_log_size)))
    {
        return;
    }

    pthread_mutex_lock(&trace_lock);
    va_list args;
    va_start (args, format);
    send_message(format, args);
    va_end (args);
    pthread_mutex_unlock(&trace_lock);
}

void trace_bytes(const uint8_t *data, uint32_t size)
{
    if (binary_upstream_fd >= 0)
    {
        ssize_t ignored = send(binary_upstream_fd, data, size, MSG_DONTWAIT);
        (void)ignored;
    }
}

void fail(const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    trace("fail: %s", message);
    fprintf(stderr, "fail: %s\n", message);

    uint32_t stack[CALL_STACK_MAX_DEPTH];
    uint32_t depth = call_stack(stack, CALL_STACK_MAX_DEPTH, 0);
    fault_init(stack, depth);
    set_restart_reason(RESTART_FAIL);

    if (os_abort_on_fail())
    {
        signal(SIGABRT, SIG_DFL);
        abort();
    }

    os_restart();
}

void trace_stack()
{
    uint32_t stack[CALL_STACK_MAX_DEPTH];
    uint32_t depth = call_stack(stack, CALL_STACK_MAX_DEPTH, 0);
    print_call_stack("trace_stack", stack, depth);
}

void set_restart_reason(restart_reason_t reason)
{
    // keep what the previous run left before overwriting it
    capture_last_restart_reason();

    saved.restart_reason = reason;
    saved.restart_magic = MAGIC_RESTART_REASON;
    saved_write();
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
    trace("force_restart: %s", restart_reason_str(reason));
    set_watchdog(0);

    os_restart();
}

void fault_init(uint32_t *callstack, uint32_t depth)
{
    // Also from signal handlers: no allocation, no stdio.
    saved_open();

    if (depth > CALL_STACK_MAX_DEPTH)
    {
        depth = CALL_STACK_MAX_DEPTH;
    }

    saved.cur_depth = depth;
    saved.last_command = MAGIC_CALLSTACK;

    for (uint32_t i=0;i<depth;++i)
    {
        saved.call_stack_buf[i] = callstack[i];
    }
    saved_write();
}

void clear_saved_crash()
{
    saved_open();
    saved.last_command = 0;
    saved_write();
}

void report_saved_crash()
{
    saved_open();

    if (saved.last_command != MAGIC_CALLSTACK)
    {
        saved.last_command = 0;
        return;
    }

    if (saved.cur_depth > CALL_STACK_MAX_DEPTH || saved.cur_depth == 0)
    {
        // corrupted data
        return;
    }

    trace("report_saved_crash: last run info, last_command: 0x%x, cur_depth: %d", saved.last_command, saved.cur_depth);
    print_call_stack("report_saved_crash", saved.call_stack_buf, saved.cur_depth);
}

int get_crash_callstack(uint32_t **callstack)
{
    saved_open();

    if (saved.last_command != MAGIC_CALLSTACK)
    {
        return 0;
    }

    if (saved.cur_depth > CALL_STACK_MAX_DEPTH || saved.cur_depth == 0)
    {
        return 0;
    }

    *callstack = saved.call_stack_buf;
    return saved.cur_depth;
}
