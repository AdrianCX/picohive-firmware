/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstddef>
#include <cstdint>

///
/// Reading HTTP header values. Kept apart from HTTPHeader, with no pico or lwip dependencies, so it builds for the host tests as well.
///
class HTTPHeaderHelper
{
public:
    enum class TransferCoding
    {
        None,
        Chunked,
        Unsupported,
    };

    static bool nextToken(const char *&cursor, const char *&token, size_t& len);
    static bool tokenIs(const char *token, size_t len, const char *name);
    static bool hasToken(const char *list, const char *name);
    static TransferCoding transferCoding(const char *list);
    static bool parseContentLength(const char *value, uint64_t& length);
    static int hexValue(uint8_t c);
    static bool getHost(const char *url, char *host, size_t size);
    static bool getPath(const char *url, char *path, size_t size);

private:
    static const char *skipScheme(const char *url);
};
