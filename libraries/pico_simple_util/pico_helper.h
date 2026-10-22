/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "os_platform.h"
#include <string_view>

class PicoHelper
{
public:
	static bool parse_number(std::string_view text, uint32_t& value);

    // Reads hex digits into out, skipping '-', true when exactly len bytes were read.
    static bool parse_hex(const char *hex, uint8_t *out, uint32_t len);

private:
    static int hex_value(char c);
};
