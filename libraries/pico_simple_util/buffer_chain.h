/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdarg.h>
#include <stdint.h>

#include <string_view>
#include <type_traits>

#ifndef BUFFER_CHAIN_BLOCK
#define BUFFER_CHAIN_BLOCK 1024
#endif

#ifndef BUFFER_CHAIN_FORMAT_STACK
#define BUFFER_CHAIN_FORMAT_STACK 256
#endif

// Chained buffers to be used when building/sending requests, avoids large allocations
class BufferChain
{
public:
    BufferChain() = default;

    // Limit on how much this can grow, buffer is truncated if reached, full() signals that if reached
    explicit BufferChain(uint32_t limit) : m_limit(limit) {}

    BufferChain(const BufferChain& other);
    BufferChain(BufferChain&& other) noexcept;
    BufferChain& operator=(const BufferChain& other);
    BufferChain& operator=(BufferChain&& other) noexcept;
    ~BufferChain();

    bool append(const void *data, uint32_t len);

    template <typename... Rest> requires (std::is_convertible_v<Rest, std::string_view> && ...)
    bool append(std::string_view text, const Rest&... rest)
    {
        return append(text.data(), (uint32_t)text.size()) && (append_one(rest) && ...);
    }

    bool append_snprintf(uint32_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
    bool append_vsnprintf(uint32_t size, const char *format, va_list ap);

	// Finish a block, start a new one at next insert, helps us insert items at a later point at the current location
    uint32_t seal();

	// Insert items at a given position, useful to put items in the middle of a string if we've sealed before
    bool insert(uint32_t sealed_at, const void *data, uint32_t len);

    uint32_t size() const;
    bool empty() const { return size() == 0; }
    bool full() const;

    // Decrement ref count on all blocks and clear this structure so it can start fresh
    void clear();

    // Each block in order, without copying: piece(const char *data, uint32_t len).
    template <typename F> void for_each(F piece) const
    {
        for (const Block *block = (m_head != nullptr) ? m_head->first : nullptr; block != nullptr; block = block->next)
        {
            if (block->used > 0)
            {
                piece(block->text(), (uint32_t)block->used);
            }
        }
    }

    // Up to len bytes from offset into out, for traces. The bytes copied.
    uint32_t copy_out(uint32_t offset, char *out, uint32_t len) const;

    uint32_t piece(uint32_t offset, const char *&data) const;

private:
    bool append_one(std::string_view text) { return append(text.data(), (uint32_t)text.size()); }

    struct Block
    {
        Block *next;
        uint16_t used;
        uint16_t capacity;

        char *text() { return (char *)(this + 1); }
        const char *text() const { return (const char *)(this + 1); }
    };

    struct Head
    {
        uint32_t refs;
        uint32_t size;
        bool full;
        Block *first;
        Block *last;
    };

    bool start();
    Block *new_block(uint32_t capacity);
    bool add_block(uint32_t capacity);
    void release();

    Head *m_head = nullptr;
    uint32_t m_limit = UINT32_MAX;
};
