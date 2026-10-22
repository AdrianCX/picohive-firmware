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

class SharedBuffer
{
public:
    SharedBuffer() = default;

    explicit SharedBuffer(uint32_t capacity);

    SharedBuffer(const SharedBuffer& other);
    SharedBuffer(SharedBuffer&& other) noexcept;
    SharedBuffer& operator=(const SharedBuffer& other);
    SharedBuffer& operator=(SharedBuffer&& other) noexcept;
    ~SharedBuffer();

    explicit operator bool() const { return m_block != nullptr; }

    uint32_t capacity() const { return m_block ? m_block->capacity : 0; }
    uint32_t size() const { return m_block ? m_block->size : 0; }
    bool empty() const { return size() == 0; }
    bool full() const { return m_block && (m_block->size == m_block->capacity); }

    // How many SharedBuffers share the block (0 when empty).
    uint32_t use_count() const { return m_block ? m_block->refs : 0; }

    const char *c_str() const { return m_block ? m_block->text() : ""; }
    std::string_view view() const { return std::string_view(c_str(), size()); }

    bool snprintf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    bool append_snprintf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    bool append_vsnprintf(const char *format, va_list ap);

    bool append(const void *data, uint32_t len);

    template <typename... Rest> requires (std::is_convertible_v<Rest, std::string_view> && ...)
    bool append(std::string_view text, const Rest&... rest)
    {
        return append(text.data(), (uint32_t)text.size()) && (append_one(rest) && ...);
    }

    bool reserve(uint32_t capacity);

    void clear() { set_size(0); }
    char *data() { return m_block ? m_block->text() : nullptr; }
    void set_size(uint32_t size);

private:
    bool append_one(std::string_view text) { return append(text.data(), (uint32_t)text.size()); }

    struct Block
    {
        uint32_t refs;
        uint32_t capacity;
        uint32_t size;

        char *text() { return (char *)(this + 1); }
    };

    void release();

    Block *m_block = nullptr;
};
