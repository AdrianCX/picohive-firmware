/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <malloc.h>
#include <stdlib.h>

#include <cstddef>
#include <memory>
#include <new>
#include <typeinfo>

#include "pico.h"

namespace std
{
void __throw_bad_exception() { panic("std::bad_exception"); }
void __throw_bad_alloc() { panic("std::bad_alloc"); }
void __throw_bad_cast() { panic("std::bad_cast"); }
void __throw_bad_typeid() { panic("std::bad_typeid"); }
void __throw_logic_error(const char *what) { panic("std::logic_error: %s", what); }
void __throw_domain_error(const char *what) { panic("std::domain_error: %s", what); }
void __throw_invalid_argument(const char *what) { panic("std::invalid_argument: %s", what); }
void __throw_length_error(const char *what) { panic("std::length_error: %s", what); }
void __throw_out_of_range(const char *what) { panic("std::out_of_range: %s", what); }
void __throw_out_of_range_fmt(const char *fmt, ...) { panic("std::out_of_range: %s", fmt); }
void __throw_runtime_error(const char *what) { panic("std::runtime_error: %s", what); }
void __throw_range_error(const char *what) { panic("std::range_error: %s", what); }
void __throw_overflow_error(const char *what) { panic("std::overflow_error: %s", what); }
void __throw_underflow_error(const char *what) { panic("std::underflow_error: %s", what); }
void __throw_ios_failure(const char *what) { panic("std::ios_base::failure: %s", what); }
void __throw_ios_failure(const char *what, int) { panic("std::ios_base::failure: %s", what); }
void __throw_system_error(int code) { panic("std::system_error: %d", code); }
void __throw_future_error(int code) { panic("std::future_error: %d", code); }
void __throw_bad_function_call() { panic("std::bad_function_call"); }
void __throw_bad_array_new_length() { panic("std::bad_array_new_length"); }
}

bool std::_Sp_make_shared_tag::_S_eq(const std::type_info &) noexcept { return false; }

extern "C" void __cxa_pure_virtual() { panic("pure virtual call"); }

static void *aligned_new(std::size_t size, std::align_val_t align)
{
    void *p = memalign(static_cast<std::size_t>(align), size);
    if (p == nullptr)
    {
        panic("aligned new: out of memory, size[%u] align[%u]", (unsigned int)size, (unsigned int)align);
    }
    return p;
}

void *operator new(std::size_t size, std::align_val_t align) { return aligned_new(size, align); }
void *operator new[](std::size_t size, std::align_val_t align) { return aligned_new(size, align); }

void *operator new(std::size_t size, const std::nothrow_t&) noexcept { return malloc(size); }
void *operator new[](std::size_t size, const std::nothrow_t&) noexcept { return malloc(size); }

void operator delete(void *p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t&) noexcept { free(p); }

void operator delete(void *p, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { free(p); }
