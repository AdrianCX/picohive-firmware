/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>

#include "pico_helper.h"

bool PicoHelper::parse_number(std::string_view text, uint32_t& value)
{
    if (text.empty() || (text.size() > 5))
    {
        return false;
    }

    uint32_t number = 0;

    for (char digit : text)
    {
        if ((digit < '0') || (digit > '9'))
        {
            return false;
        }

        number = (number * 10) + (digit - '0');
    }

    value = number;
    return true;
}

int PicoHelper::hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool PicoHelper::parse_hex(const char *hex, uint8_t *out, uint32_t len)
{
    uint32_t digits = 0;
    for (const char *c = hex; *c != 0; ++c)
    {
        if (*c == '-')
        {
            continue;
        }
        int v = hex_value(*c);
        if (v < 0 || digits >= len * 2)
        {
            return false;
        }
        out[digits / 2] = (digits % 2 == 0) ? (v << 4) : (out[digits / 2] | v);
        digits++;
    }
    return digits == len * 2;
}

void PicoHelper::hex_into(char *out, size_t size, const uint8_t *data, uint32_t count)
{
    size_t pos = 0;

    for (uint32_t i = 0; (i < count) && (pos + 4 < size); ++i)
    {
        pos += snprintf(&out[pos], size - pos, "%s%02X", (i > 0) ? " " : "", data[i]);
    }

    out[(pos < size) ? pos : (size - 1)] = 0;
}
