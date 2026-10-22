/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "os_loop.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "os_linux.h"
#include "pico_logger.h"

namespace
{

struct Watch
{
    uint64_t token;
    int fd;
    short events;
    OsPollClient *client;
};

struct Task
{
    uint64_t token;
    std::function<void()> fn;
};

struct Lookup
{
    uint64_t token;
    std::string host;
    std::function<void(err_t, ip_addr_t)> done;
};

struct State
{
    uint64_t next_token = 1;
    std::unordered_set<uint64_t> live_tokens;
    std::vector<Watch> watches;

    std::vector<pollfd> fds;
    std::vector<Watch> polled;
    std::deque<Task> due;

    std::mutex tasks_lock;
    std::deque<Task> tasks;
    int wake_fd = -1;

    std::mutex lookups_lock;
    std::condition_variable lookups_ready;
    std::deque<Lookup> lookups;
    std::thread resolver;
    bool resolver_stop = false;
};

State& state()
{
    static State *s = new State();
    return *s;
}

Watch *find(int fd)
{
    for (Watch& watch : state().watches)
    {
        if (watch.fd == fd)
        {
            return &watch;
        }
    }
    return nullptr;
}

void wake()
{
    if (state().wake_fd >= 0)
    {
        uint64_t one = 1;
        ssize_t ignored = write(state().wake_fd, &one, sizeof(one));
        (void)ignored;
    }
}

void resolver_thread()
{
    State& s = state();

    while (true)
    {
        Lookup lookup;
        {
            std::unique_lock<std::mutex> lock(s.lookups_lock);
            s.lookups_ready.wait(lock, [&s]() { return !s.lookups.empty() || s.resolver_stop; });
            if (s.lookups.empty())
            {
                return;
            }
            lookup = std::move(s.lookups.front());
            s.lookups.pop_front();
        }

        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo *result = nullptr;
        int error = getaddrinfo(lookup.host.c_str(), nullptr, &hints, &result);

        err_t err = ERR_OK;
        ip_addr_t addr = 0;

        if ((error != 0) || (result == nullptr))
        {
            err = ERR_VAL;
        }
        else
        {
            addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
        }

        if (result != nullptr)
        {
            freeaddrinfo(result);
        }

        auto done = std::move(lookup.done);
        OsLoop::post(lookup.token, [done, err, addr]() { done(err, addr); });
    }
}

void resolver_stop()
{
    State& s = state();
    {
        std::lock_guard<std::mutex> lock(s.lookups_lock);
        s.resolver_stop = true;
        s.lookups.clear();
    }
    s.lookups_ready.notify_one();

    if (s.resolver.joinable())
    {
        s.resolver.join();
    }
}

}

uint64_t OsLoop::acquire_token()
{
    uint64_t token = state().next_token++;
    state().live_tokens.insert(token);
    return token;
}

void OsLoop::release_token(uint64_t token)
{
    state().live_tokens.erase(token);

    std::vector<Watch>& watches = state().watches;
    for (size_t i = 0; i < watches.size();)
    {
        if (watches[i].token == token)
        {
            watches.erase(watches.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

bool OsLoop::alive(uint64_t token)
{
    return (token == 0) || (state().live_tokens.count(token) != 0);
}

void OsLoop::watch(uint64_t token, int fd, short events, OsPollClient *client)
{
    Watch *existing = find(fd);

    if (existing != nullptr)
    {
        existing->token = token;
        existing->events = events;
        existing->client = client;
        return;
    }

    state().watches.push_back(Watch{token, fd, events, client});
}

void OsLoop::unwatch(int fd)
{
    std::vector<Watch>& watches = state().watches;
    for (size_t i = 0; i < watches.size(); ++i)
    {
        if (watches[i].fd == fd)
        {
            watches.erase(watches.begin() + i);
            return;
        }
    }
}

void OsLoop::post(uint64_t token, std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lock(state().tasks_lock);
        state().tasks.push_back(Task{token, std::move(fn)});
    }
    wake();
}

void OsLoop::resolve(uint64_t token, const char *host, std::function<void(err_t err, ip_addr_t addr)> done)
{
    State& s = state();

    {
        std::lock_guard<std::mutex> lock(s.lookups_lock);
        s.lookups.push_back(Lookup{token, (host != nullptr) ? host : "", std::move(done)});

        if (!s.resolver.joinable())
        {
            // Blocked in a wait when idle, joined at exit.
            s.resolver = std::thread(resolver_thread);
            atexit(resolver_stop);
        }
    }
    s.lookups_ready.notify_one();
}

void OsLoop::run_once(int timeout_ms)
{
    State& s = state();

    if (s.wake_fd < 0)
    {
        s.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }

    bool queued;
    {
        std::lock_guard<std::mutex> lock(s.tasks_lock);
        queued = !s.tasks.empty();
    }

    std::vector<pollfd>& fds = s.fds;
    std::vector<Watch>& polled = s.polled;
    fds.clear();
    polled.clear();

    for (const Watch& watch : s.watches)
    {
        fds.push_back(pollfd{watch.fd, watch.events, 0});
        polled.push_back(watch);
    }

    fds.push_back(pollfd{s.wake_fd, POLLIN, 0});

    int ready = poll(fds.data(), fds.size(), queued ? 0 : timeout_ms);

    if ((ready < 0) && (errno != EINTR))
    {
        trace("OsLoop::run_once: poll failed, errno=%d", errno);
    }

    if (fds.back().revents & POLLIN)
    {
        uint64_t count;
        ssize_t ignored = read(s.wake_fd, &count, sizeof(count));
        (void)ignored;
    }

    for (size_t i = 0; (ready > 0) && (i < polled.size()); ++i)
    {
        if (fds[i].revents == 0)
        {
            continue;
        }

        Watch *current = find(polled[i].fd);
        if ((current == nullptr) || (current->token != polled[i].token) || (current->client != polled[i].client) || !alive(polled[i].token))
        {
            continue;
        }

        current->client->on_poll(fds[i].revents);
    }

    std::deque<Task>& due = s.due;
    {
        std::lock_guard<std::mutex> lock(s.tasks_lock);
        due.swap(s.tasks);
    }

    while (!due.empty())
    {
        Task task = std::move(due.front());
        due.pop_front();

        if (alive(task.token))
        {
            task.fn();
        }
    }
}

extern "C" void os_resolve(const char *host, void (*done)(int err, uint32_t addr, void *context), void *context)
{
    OsLoop::resolve(0, host, [done, context](err_t err, ip_addr_t addr) { done(err, addr, context); });
}
