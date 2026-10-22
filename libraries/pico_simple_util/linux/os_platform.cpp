/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "os_platform.h"
#include "os_linux.h"
#include "pico_logger.h"

namespace
{

std::string data_dir;
std::string exe_path;
std::vector<std::string> saved_args;
char **restart_args = nullptr;

int port_offset = 8000;
int loop_ms = 5;
bool trace_stdout = false;
bool exit_on_restart = false;
bool abort_on_fail = false;
bool no_watchdog = false;

volatile sig_atomic_t exit_requested = 0;
volatile sig_atomic_t exit_code = 0;

bool on_loop_thread_set = false;
pthread_t loop_thread;

struct timespec start_time = {};

void usage(const char *name)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "\n"
        "  --data-dir DIR       Folder for device fs, default: <executable>_data\n"
        "  --trace-stdout       Print every trace() to stdout, unencrypted\n"
        "  --port-offset N      Apply to listen ports (default: 8000, 80 -> 8080, 443 -> 8443)\n"
        "  --loop-ms N          longest wait in the loop when idle (default: 5)\n"
        "  --exit-on-restart    a restart exits with status 3 instead of exec'ing again, for valgrind\n"
        "  --abort-on-fail      fail()/panic() abort() instead of restarting, for gdb\n"
        "  --no-watchdog        ignore the app's watchdog_timeout, for gdb\n"
        "  --help\n",
        name);
}

bool under_valgrind()
{
    const char *preload = getenv("LD_PRELOAD");
    return (preload != nullptr) && (strstr(preload, "vgpreload") != nullptr);
}

void on_exit_signal(int sig)
{
    if (exit_requested)
    {
        _exit(128 + sig);
    }
    exit_requested = 1;
    exit_code = 0;
}

std::string self_exe()
{
    char path[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0)
    {
        return "";
    }
    path[len] = 0;
    return path;
}

}

extern "C" {

void os_platform_init(int argc, char **argv)
{
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int i = 0; i < argc; ++i)
    {
        saved_args.push_back(argv[i]);
    }

    restart_args = new char *[saved_args.size() + 1];
    for (size_t i = 0; i < saved_args.size(); ++i)
    {
        restart_args[i] = &saved_args[i][0];
    }
    restart_args[saved_args.size()] = nullptr;

    exe_path = self_exe();

    static const struct option options[] = {
        {"data-dir", required_argument, nullptr, 'd'},
        {"trace-stdout", no_argument, nullptr, 't'},
        {"port-offset", required_argument, nullptr, 'p'},
        {"loop-ms", required_argument, nullptr, 'l'},
        {"exit-on-restart", no_argument, nullptr, 'x'},
        {"abort-on-fail", no_argument, nullptr, 'a'},
        {"no-watchdog", no_argument, nullptr, 'w'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", options, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'd': data_dir = optarg; break;
            case 't': trace_stdout = true; break;
            case 'p': port_offset = atoi(optarg); break;
            case 'l': loop_ms = atoi(optarg); break;
            case 'x': exit_on_restart = true; break;
            case 'a': abort_on_fail = true; break;
            case 'w': no_watchdog = true; break;
            case 'h': usage(argv[0]); exit(0);
            default: usage(argv[0]); exit(2);
        }
    }

    if (under_valgrind())
    {
        exit_on_restart = true;
    }

    if (data_dir.empty())
    {
        data_dir = (exe_path.empty() ? std::string(argv[0]) : exe_path) + "_data";
    }

    if ((mkdir(data_dir.c_str(), 0700) != 0) && (errno != EEXIST))
    {
        fprintf(stderr, "cannot create data directory %s: %s\n", data_dir.c_str(), strerror(errno));
        exit(2);
    }

    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa = {};
    sa.sa_handler = on_exit_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    setvbuf(stdout, nullptr, _IOLBF, 0);
}

const char *os_data_dir(void)
{
    return data_dir.c_str();
}

const char *os_exe_path(void)
{
    return exe_path.c_str();
}

uint16_t os_map_port(uint16_t port)
{
    if ((port != 0) && (port < 1024) && (port_offset > 0))
    {
        return (uint16_t)(port + port_offset);
    }
    return port;
}

int os_loop_ms(void)
{
    return loop_ms;
}

bool os_trace_stdout(void)
{
    return trace_stdout;
}

bool os_abort_on_fail(void)
{
    return abort_on_fail;
}

bool os_watchdog_disabled(void)
{
    return no_watchdog;
}

bool os_exit_on_restart(void)
{
    return exit_on_restart || exe_path.empty();
}

void os_restart(void)
{
    fflush(stdout);
    fflush(stderr);

    if (os_exit_on_restart())
    {
        fprintf(stderr, "restart: exiting with status 3 (--exit-on-restart or valgrind)\n");

        if (on_loop_thread_set && pthread_equal(pthread_self(), loop_thread))
        {
            exit_code = 3;
            pthread_exit(nullptr);
        }
        exit(3);
    }

    execv(exe_path.c_str(), restart_args);

    fprintf(stderr, "restart: exec %s failed: %s, exiting\n", exe_path.c_str(), strerror(errno));
    _exit(3);
}

void os_restart_from_signal(void)
{
    if (!os_exit_on_restart())
    {
        execv(exe_path.c_str(), restart_args);
    }
    _exit(3);
}

bool os_exit_requested(void)
{
    return exit_requested != 0;
}

void os_request_exit(int code)
{
    exit_code = code;
    exit_requested = 1;
}

int os_exit_code(void)
{
    return exit_code;
}

namespace
{

uint32_t *stack_bottom = nullptr;
uint32_t stack_size = 0;

struct StackRun
{
    int (*fn)(void *);
    void *arg;
    int result;
    bool returned;
};

void *stack_thread(void *arg)
{
    StackRun *run = (StackRun *)arg;
    run->result = run->fn(run->arg);
    run->returned = true;
    return nullptr;
}

}

extern "C" int os_run_on_stack(uint32_t size, int (*fn)(void *), void *arg)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t wanted = (size < (uint32_t)PTHREAD_STACK_MIN) ? (size_t)PTHREAD_STACK_MIN : size;
    wanted = (wanted + page - 1) & ~(size_t)(page - 1);

    if (wanted != size)
    {
        fprintf(stderr, "stack: %u bytes asked for, %zu used (the system's minimum, whole pages)\n", (unsigned int)size, wanted);
    }

    // A guard page below the stack: running off its end faults right there.
    uint8_t *region = (uint8_t *)mmap(nullptr, wanted + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (region == MAP_FAILED)
    {
        fprintf(stderr, "stack: cannot map %zu bytes: %s\n", wanted, strerror(errno));
        return 1;
    }
    mprotect(region, page, PROT_NONE);

    stack_bottom = (uint32_t *)(region + page);
    stack_size = (uint32_t)wanted;

    // Painted as the board paints its stack (memory_stats.cpp), so the most used shows.
    for (uint32_t i = 0; i < stack_size / sizeof(uint32_t); ++i)
    {
        stack_bottom[i] = 0xDEADBEEF;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack_bottom, wanted);

    StackRun run = {fn, arg, 1, false};
    if (pthread_create(&loop_thread, &attr, stack_thread, &run) != 0)
    {
        fprintf(stderr, "stack: cannot start the loop thread: %s\n", strerror(errno));
        return 1;
    }
    on_loop_thread_set = true;
    pthread_attr_destroy(&attr);

    pthread_join(loop_thread, nullptr);

    // Ended with pthread_exit() by a restart that exits (os_restart()).
    return run.returned ? run.result : (int)exit_code;
}

extern "C" void os_stack_region(uint32_t **bottom, uint32_t *size)
{
    *bottom = stack_bottom;
    *size = stack_size;
}

uint64_t time_us_64(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    int64_t sec = now.tv_sec - start_time.tv_sec;
    int64_t nsec = now.tv_nsec - start_time.tv_nsec;
    return (uint64_t)(sec * 1000000LL + nsec / 1000);
}

void sleep_us(uint64_t us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;

    while ((nanosleep(&ts, &ts) != 0) && (errno == EINTR))
    {
    }
}

void sleep_ms(uint32_t ms)
{
    sleep_us((uint64_t)ms * 1000);
}

uint64_t get_rand_64(void)
{
    uint64_t value = 0;
    uint8_t *p = (uint8_t *)&value;
    size_t got = 0;

    while (got < sizeof(value))
    {
        ssize_t n = getrandom(p + got, sizeof(value) - got, 0);
        if (n > 0)
        {
            got += n;
        }
        else if (errno != EINTR)
        {
            panic("getrandom failed: %s", strerror(errno));
        }
    }
    return value;
}

uint32_t get_rand_32(void)
{
    return (uint32_t)get_rand_64();
}

void os_unique_id(uint8_t id_out[OS_UNIQUE_ID_SIZE])
{
    static bool loaded = false;
    static uint8_t id[OS_UNIQUE_ID_SIZE] = {};

    if (!loaded)
    {
        std::string path = data_dir + "/instance_id";
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);

        if ((fd < 0) || (read(fd, id, sizeof(id)) != (ssize_t)sizeof(id)))
        {
            if (fd >= 0)
            {
                close(fd);
            }

            uint64_t r = get_rand_64();
            memcpy(id, &r, sizeof(id));

            fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if ((fd < 0) || (write(fd, id, sizeof(id)) != (ssize_t)sizeof(id)))
            {
                panic("cannot write %s: %s", path.c_str(), strerror(errno));
            }
        }

        close(fd);
        loaded = true;
    }

    memcpy(id_out, id, sizeof(id));
}

void panic(const char *fmt, ...)
{
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    fail("panic: %s", message);
}

const char *lwip_strerr(err_t err)
{
    switch (err)
    {
        case ERR_OK: return "Ok.";
        case ERR_MEM: return "Out of memory error.";
        case ERR_BUF: return "Buffer error.";
        case ERR_TIMEOUT: return "Timeout.";
        case ERR_RTE: return "Routing problem.";
        case ERR_INPROGRESS: return "Operation in progress.";
        case ERR_VAL: return "Illegal value.";
        case ERR_WOULDBLOCK: return "Operation would block.";
        case ERR_USE: return "Address in use.";
        case ERR_ALREADY: return "Already connecting.";
        case ERR_ISCONN: return "Already connected.";
        case ERR_CONN: return "Not connected.";
        case ERR_IF: return "Low-level netif error.";
        case ERR_ABRT: return "Connection aborted.";
        case ERR_RST: return "Connection reset.";
        case ERR_CLSD: return "Connection closed.";
        case ERR_ARG: return "Illegal argument.";
    }
    return "Unknown error.";
}

}

namespace
{

const unsigned int MAX_PINS = 64;

struct Pin
{
    bool out = false;
    bool level = false;
    bool pull_up = false;
};

Pin pins[MAX_PINS];

Pin *pin(unsigned int gpio)
{
    if (gpio >= MAX_PINS)
    {
        panic("gpio %u does not exist", gpio);
    }
    return &pins[gpio];
}

}

extern "C" {

void gpio_init(unsigned int gpio)
{
    *pin(gpio) = Pin();
}

void gpio_set_dir(unsigned int gpio, bool out)
{
    pin(gpio)->out = out;
}

void gpio_put(unsigned int gpio, bool value)
{
    Pin *p = pin(gpio);
    if (p->level != value)
    {
        trace("gpio: %u -> %d", gpio, value ? 1 : 0);
    }
    p->level = value;
}

bool gpio_get(unsigned int gpio)
{
    Pin *p = pin(gpio);
    return p->out ? p->level : p->pull_up;
}

void gpio_set_pulls(unsigned int gpio, bool up, bool down)
{
    (void)down;
    pin(gpio)->pull_up = up;
}

void pwm_set_chan_level(unsigned int slice, unsigned int channel, uint16_t level)
{
    static uint16_t levels[8][2] = {};

    if ((slice >= 8) || (channel >= 2))
    {
        panic("pwm slice %u channel %u does not exist", slice, channel);
    }
    if (levels[slice][channel] != level)
    {
        trace("pwm: slice %u %c -> %u", slice, (channel == PWM_CHAN_A) ? 'A' : 'B', (unsigned int)level);
    }
    levels[slice][channel] = level;
}

}
