/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer_chain.h"

static_assert(BUFFER_CHAIN_BLOCK <= 0xFFFF, "a block's size has to fit its 16 bit fields");

BufferChain::BufferChain(const BufferChain& other)
    : m_head(other.m_head)
    , m_limit(other.m_limit)
{
    if (m_head != nullptr)
    {
        ++m_head->refs;
    }
}

BufferChain::BufferChain(BufferChain&& other) noexcept
    : m_head(other.m_head)
    , m_limit(other.m_limit)
{
    other.m_head = nullptr;
}

BufferChain& BufferChain::operator=(const BufferChain& other)
{
    if (m_head != other.m_head)
    {
        release();
        m_head = other.m_head;

        if (m_head != nullptr)
        {
            ++m_head->refs;
        }
    }

    m_limit = other.m_limit;
    return *this;
}

BufferChain& BufferChain::operator=(BufferChain&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_head = other.m_head;
        m_limit = other.m_limit;
        other.m_head = nullptr;
    }

    return *this;
}

BufferChain::~BufferChain()
{
    release();
}

void BufferChain::release()
{
    if ((m_head != nullptr) && (--m_head->refs == 0))
    {
        Block *block = m_head->first;

        while (block != nullptr)
        {
            Block *next = block->next;
            free(block);
            block = next;
        }

        free(m_head);
    }

    m_head = nullptr;
}

void BufferChain::clear()
{
    release();
}

bool BufferChain::start()
{
    if (m_head != nullptr)
    {
        return true;
    }

    m_head = (Head *)malloc(sizeof(Head));

    if (m_head == nullptr)
    {
        return false;
    }

    *m_head = Head{ 1, 0, false, nullptr, nullptr };
    return true;
}

BufferChain::Block *BufferChain::new_block(uint32_t capacity)
{
    Block *block = (Block *)malloc(sizeof(Block) + capacity);

    if (block != nullptr)
    {
        *block = Block{ nullptr, 0, (uint16_t)capacity };
    }

    return block;
}

bool BufferChain::add_block(uint32_t capacity)
{
    Block *block = new_block(capacity);

    if (block == nullptr)
    {
        m_head->full = true;
        return false;
    }

    if (m_head->last == nullptr)
    {
        m_head->first = block;
    }
    else
    {
        m_head->last->next = block;
    }

    m_head->last = block;
    return true;
}

uint32_t BufferChain::size() const
{
    return (m_head != nullptr) ? m_head->size : 0;
}

bool BufferChain::full() const
{
    return (m_head != nullptr) && m_head->full;
}

bool BufferChain::append(const void *data, uint32_t len)
{
    if (!start())
    {
        return false;
    }

    if (m_head->full || (len > (m_limit - m_head->size)))
    {
        m_head->full = true;
        return false;
    }

    const char *from = (const char *)data;

    while (len > 0)
    {
        if (((m_head->last == nullptr) || (m_head->last->used == m_head->last->capacity)) && !add_block(BUFFER_CHAIN_BLOCK))
        {
            return false;
        }

        Block *last = m_head->last;
        uint32_t room = last->capacity - last->used;
        uint32_t take = (len < room) ? len : room;

        memcpy(&last->text()[last->used], from, take);
        last->used += take;
        m_head->size += take;
        from += take;
        len -= take;
    }

    return true;
}

bool BufferChain::append_snprintf(uint32_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    bool fit = append_vsnprintf(size, format, ap);
    va_end(ap);

    return fit;
}

bool BufferChain::append_vsnprintf(uint32_t size, const char *format, va_list ap)
{
    if (!start())
    {
        return false;
    }

    if (m_head->full || (size >= 0xFFFF))
    {
        m_head->full = true;
        return false;
    }

    Block *last = m_head->last;

    if ((last == nullptr) || ((uint32_t)(last->capacity - last->used) <= size))
    {
        if (size < BUFFER_CHAIN_FORMAT_STACK)
        {
            char text[BUFFER_CHAIN_FORMAT_STACK];
            int len = vsnprintf(text, size + 1, format, ap);

            if ((len < 0) || ((uint32_t)len > size))
            {
                m_head->full = true;
                return false;
            }

            return append(text, (uint32_t)len);
        }

        if (!add_block((size + 1 > BUFFER_CHAIN_BLOCK) ? size + 1 : BUFFER_CHAIN_BLOCK))
        {
            return false;
        }

        last = m_head->last;
    }

    int len = vsnprintf(&last->text()[last->used], size + 1, format, ap);

    if ((len < 0) || ((uint32_t)len > size) || ((uint32_t)len > (m_limit - m_head->size)))
    {
        m_head->full = true;
        return false;
    }

    last->used += len;
    m_head->size += len;
    return true;
}

uint32_t BufferChain::seal()
{
    if ((m_head != nullptr) && (m_head->last != nullptr))
    {
        m_head->last->capacity = m_head->last->used;
    }

    return size();
}

bool BufferChain::insert(uint32_t sealed_at, const void *data, uint32_t len)
{
    if ((len == 0) || !start())
    {
        return len == 0;
    }

    if (m_head->full || (len > (m_limit - m_head->size)) || (len > 0xFFFF))
    {
        m_head->full = true;
        return false;
    }

    Block *before = nullptr;
    uint32_t at = 0;

    if (sealed_at > 0)
    {
        for (before = m_head->first; before != nullptr; before = before->next)
        {
            at += before->used;

            if (at >= sealed_at)
            {
                break;
            }
        }

        if ((before == nullptr) || (at != sealed_at))
        {
            return false;
        }
    }

    Block *block = new_block(len);

    if (block == nullptr)
    {
        m_head->full = true;
        return false;
    }

    memcpy(block->text(), data, len);
    block->used = (uint16_t)len;

    if (before == nullptr)
    {
        block->next = m_head->first;
        m_head->first = block;
    }
    else
    {
        block->next = before->next;
        before->next = block;
    }

    if (block->next == nullptr)
    {
        m_head->last = block;
    }

    m_head->size += len;
    return true;
}

uint32_t BufferChain::copy_out(uint32_t offset, char *out, uint32_t len) const
{
    uint32_t copied = 0;
    uint32_t at = 0;

    for (const Block *block = (m_head != nullptr) ? m_head->first : nullptr; (block != nullptr) && (copied < len); block = block->next)
    {
        uint32_t end = at + block->used;

        if (end > offset)
        {
            uint32_t from = (offset > at) ? (offset - at) : 0;
            uint32_t take = block->used - from;
            take = (take < (len - copied)) ? take : (len - copied);

            memcpy(&out[copied], &block->text()[from], take);
            copied += take;
            offset += take;
        }

        at = end;
    }

    return copied;
}

uint32_t BufferChain::piece(uint32_t offset, const char *&data) const
{
    uint32_t at = 0;

    for (const Block *block = (m_head != nullptr) ? m_head->first : nullptr; block != nullptr; block = block->next)
    {
        if (offset < (at + block->used))
        {
            data = &block->text()[offset - at];
            return at + block->used - offset;
        }

        at += block->used;
    }

    data = nullptr;
    return 0;
}
