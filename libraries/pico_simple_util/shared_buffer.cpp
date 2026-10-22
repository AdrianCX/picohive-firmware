/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <type_traits>

#include "shared_buffer.h"

SharedBuffer::SharedBuffer(uint32_t capacity)
{
    void *memory = malloc(sizeof(Block) + capacity + 1);

    if (memory != nullptr)
    {
        m_block = new (memory) Block{ 1, capacity, 0 };
        m_block->text()[0] = 0;
    }
}

SharedBuffer::SharedBuffer(const SharedBuffer& other)
    : m_block(other.m_block)
{
    if (m_block != nullptr)
    {
        ++m_block->refs;
    }
}

SharedBuffer::SharedBuffer(SharedBuffer&& other) noexcept
    : m_block(other.m_block)
{
    other.m_block = nullptr;
}

SharedBuffer& SharedBuffer::operator=(const SharedBuffer& other)
{
    if (m_block != other.m_block)
    {
        release();
        m_block = other.m_block;

        if (m_block != nullptr)
        {
            ++m_block->refs;
        }
    }

    return *this;
}

SharedBuffer& SharedBuffer::operator=(SharedBuffer&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_block = other.m_block;
        other.m_block = nullptr;
    }

    return *this;
}

SharedBuffer::~SharedBuffer()
{
    release();
}

void SharedBuffer::release()
{
    static_assert(std::is_trivially_destructible_v<Block>, "the block is freed, never destroyed");

    if ((m_block != nullptr) && (--m_block->refs == 0))
    {
        free(m_block);
    }

    m_block = nullptr;
}

bool SharedBuffer::snprintf(const char *format, ...)
{
    clear();

    va_list ap;
    va_start(ap, format);
    bool fit = append_vsnprintf(format, ap);
    va_end(ap);

    return fit;
}

bool SharedBuffer::append_snprintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    bool fit = append_vsnprintf(format, ap);
    va_end(ap);

    return fit;
}

bool SharedBuffer::append_vsnprintf(const char *format, va_list ap)
{
    if (m_block == nullptr)
    {
        return false;
    }

    uint32_t room = m_block->capacity - m_block->size;
    int written = ::vsnprintf(m_block->text() + m_block->size, room + 1, format, ap);

    if (written < 0)
    {
        m_block->text()[m_block->size] = 0;
        return false;
    }

    m_block->size += ((uint32_t)written <= room) ? (uint32_t)written : room;
    return (uint32_t)written <= room;
}

bool SharedBuffer::append(const void *data, uint32_t len)
{
    if (m_block == nullptr)
    {
        return false;
    }

    uint32_t room = m_block->capacity - m_block->size;
    uint32_t taken = (len <= room) ? len : room;

    if (taken > 0)
    {
        memcpy(m_block->text() + m_block->size, data, taken);
        m_block->size += taken;
    }
    m_block->text()[m_block->size] = 0;

    return taken == len;
}

bool SharedBuffer::reserve(uint32_t capacity)
{
    if (m_block == nullptr)
    {
        *this = SharedBuffer(capacity);
        return m_block != nullptr;
    }

    if (m_block->refs != 1)
    {
        return false;
    }

    if (capacity <= m_block->capacity)
    {
        return true;
    }

    Block *grown = (Block *)realloc(m_block, sizeof(Block) + capacity + 1);

    if (grown == nullptr)
    {
        return false;
    }

    m_block = grown;
    m_block->capacity = capacity;
    return true;
}

void SharedBuffer::set_size(uint32_t size)
{
    if (m_block == nullptr)
    {
        return;
    }

    m_block->size = (size < m_block->capacity) ? size : m_block->capacity;
    m_block->text()[m_block->size] = 0;
}
