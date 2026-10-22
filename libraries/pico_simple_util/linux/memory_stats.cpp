/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include <malloc.h>
#include <stddef.h>
#include <sys/resource.h>

#include "memory_stats.h"
#include "os_linux.h"

#if __has_include(<valgrind/valgrind.h>)
#include <valgrind/valgrind.h>
#else
#define RUNNING_ON_VALGRIND 0
#endif

static const uint32_t HEAP_BUDGET = 1024u * 1024u * 1024u;

static uint32_t min_free_heap = UINT32_MAX;

static uint32_t clamp(size_t value)
{
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t)value;
}

void memory_stats_init()
{
}

uint32_t getTotalHeap()
{
    return HEAP_BUDGET;
}

uint32_t getUsedHeap()
{
    struct mallinfo2 m = mallinfo2();
    return clamp(m.uordblks + m.hblkhd);
}

uint32_t getFreeHeap()
{
    uint32_t used = getUsedHeap();
    uint32_t free_heap = (used < HEAP_BUDGET) ? (HEAP_BUDGET - used) : 0;

    if (free_heap < min_free_heap)
    {
        min_free_heap = free_heap;
    }

    return free_heap;
}

uint32_t getMinFreeHeap()
{
    return min_free_heap;
}

uint32_t getLargestFreeBlock()
{
    return getFreeHeap();
}

uint32_t getTotalStack()
{
    uint32_t *bottom = nullptr;
    uint32_t size = 0;
    os_stack_region(&bottom, &size);
    return size;
}

uint32_t getMaxUsedStack()
{
    uint32_t *bottom = nullptr;
    uint32_t size = 0;
    os_stack_region(&bottom, &size);

    if (RUNNING_ON_VALGRIND)
    {
        return 0;
    }

    uint32_t words = size / sizeof(uint32_t);
    uint32_t untouched = 0;
    while ((untouched < words) && (bottom[untouched] == 0xDEADBEEF))
    {
        untouched++;
    }
    return (words - untouched) * sizeof(uint32_t);
}
