/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <malloc.h>
#include <reent.h>
#include <stddef.h>

#include "memory_stats.h"

extern char __end__, __HeapLimit;
extern uint32_t __StackBottom, __StackTop;

extern "C" void *__real_malloc(size_t size);
extern "C" void __real_free(void *ptr);

// newlib's own record of the most the heap ever grew to (sbrk)
extern "C" unsigned long __malloc_max_sbrked_mem;

static const uint32_t STACK_PAINT = 0xDEADBEEF;
static uint32_t *stack_mark = &__StackTop;

void memory_stats_init()
{
    uint32_t *sp = (uint32_t *)__builtin_frame_address(0) - 64;
    for (uint32_t *p = &__StackBottom; p < sp; p++)
    {
        *p = STACK_PAINT;
    }

    _mallopt_r(_REENT, M_TRIM_THRESHOLD, 1024);
}

uint32_t getTotalHeap()
{
    return &__HeapLimit - &__end__;
}

uint32_t getUsedHeap()
{
    struct mallinfo m = _mallinfo_r(_REENT);
    return m.uordblks;
}

uint32_t getFreeHeap()
{
    return getTotalHeap() - getUsedHeap();
}

uint32_t getMinFreeHeap()
{
    uint32_t total = getTotalHeap();
    return (__malloc_max_sbrked_mem < total) ? total - __malloc_max_sbrked_mem : 0;
}

uint32_t getLargestFreeBlock()
{
    unsigned long peak = __malloc_max_sbrked_mem;
    uint32_t fits = 0;
    uint32_t too_big = getFreeHeap() + 1;

    while (too_big - fits > 64)
    {
        uint32_t size = fits + (too_big - fits) / 2;
        void *block = __real_malloc(size);

        if (block != NULL)
        {
            __real_free(block);
            fits = size;
        }
        else
        {
            too_big = size;
        }
    }

    __malloc_max_sbrked_mem = peak;
    return fits;
}

uint32_t getTotalStack()
{
    return (&__StackTop - &__StackBottom) * sizeof(uint32_t);
}

uint32_t getMaxUsedStack()
{
    for (uint32_t *p = &__StackBottom; p < stack_mark; p++)
    {
        if (*p != STACK_PAINT)
        {
            stack_mark = p;
            break;
        }
    }
    return (&__StackTop - stack_mark) * sizeof(uint32_t);
}
