/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <cstdlib>
#include <string.h>
#include <strings.h>

#include "http_header_helper.h"

bool HTTPHeaderHelper::nextToken(const char *&cursor, const char *&token, size_t& len)
{
    while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == ','))
    {
        ++cursor;
    }

    if (*cursor == 0)
    {
        return false;
    }

    token = cursor;

    while ((*cursor != 0) && (*cursor != ','))
    {
        ++cursor;
    }

    len = cursor - token;

    while ((len > 0) && ((token[len - 1] == ' ') || (token[len - 1] == '\t')))
    {
        --len;
    }

    return true;
}

bool HTTPHeaderHelper::tokenIs(const char *token, size_t len, const char *name)
{
    return (strlen(name) == len) && (strncasecmp(token, name, len) == 0);
}

bool HTTPHeaderHelper::hasToken(const char *list, const char *name)
{
    const char *token = NULL;
    size_t len = 0;

    for (const char *cursor = list; (cursor != NULL) && nextToken(cursor, token, len);)
    {
        if (tokenIs(token, len, name))
        {
            return true;
        }
    }

    return false;
}

HTTPHeaderHelper::TransferCoding HTTPHeaderHelper::transferCoding(const char *list)
{
    TransferCoding coding = TransferCoding::None;
    const char *token = NULL;
    size_t len = 0;

    for (const char *cursor = list; (cursor != NULL) && nextToken(cursor, token, len);)
    {
        if (coding == TransferCoding::Chunked)
        {
            return TransferCoding::Unsupported;
        }

        if (tokenIs(token, len, "chunked"))
        {
            coding = TransferCoding::Chunked;
        }
        else if (!tokenIs(token, len, "identity"))
        {
            return TransferCoding::Unsupported;
        }
    }

    return coding;
}

bool HTTPHeaderHelper::parseContentLength(const char *value, uint64_t& length)
{
    if ((value == NULL) || (value[0] < '0') || (value[0] > '9'))
    {
        return false;
    }

    char *end = NULL;
    length = strtoull(value, &end, 10);

    while ((*end == ' ') || (*end == '\t'))
    {
        ++end;
    }

    return *end == 0;
}

int HTTPHeaderHelper::hexValue(uint8_t c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }

    return -1;
}

const char *HTTPHeaderHelper::skipScheme(const char *url)
{
    if (strncasecmp(url, "https://", 8) == 0)
    {
        return url + 8;
    }

    if (strncasecmp(url, "http://", 7) == 0)
    {
        return url + 7;
    }

    return url;
}

bool HTTPHeaderHelper::getHost(const char *url, char *host, size_t size)
{
    if ((url == NULL) || (host == NULL) || (size < 1) || (url[0] == 0))
    {
        return false;
    }

    const char *p = skipScheme(url);
    size_t n = 0;

    while ((*p != 0) && (*p != '/'))
    {
        if (n >= size - 1)
        {
            return false;
        }

        host[n++] = *p++;
    }

    host[n] = 0;
    return n > 0;
}

bool HTTPHeaderHelper::getPath(const char *url, char *path, size_t size)
{
    if ((url == NULL) || (path == NULL) || (size < 2) || (url[0] == 0))
    {
        return false;
    }

    const char *p = strchr(skipScheme(url), '/');

    if (p == NULL)
    {
        p = "/";
    }

    size_t len = strlen(p);

    if (len >= size)
    {
        return false;
    }

    memcpy(path, p, len + 1);
    return true;
}
