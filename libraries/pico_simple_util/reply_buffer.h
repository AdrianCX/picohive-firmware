/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include <string_view>

#include "shared_buffer.h"

// Reply helper - combine SharedBuffer with topic taken from the context
class ReplyBuffer : public SharedBuffer
{
public:
    ReplyBuffer() = default;

    template <typename Context>
    ReplyBuffer(const Context& ctx, uint32_t capacity)
        : SharedBuffer(capacity), m_prefix(ctx.prefix), m_name(ctx.name)
    {
    }

    std::string_view prefix() const { return m_prefix; }
    std::string_view name() const { return m_name; }

private:
    std::string_view m_prefix;
    std::string_view m_name;
};
