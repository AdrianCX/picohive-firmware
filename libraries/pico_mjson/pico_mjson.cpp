/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "pico_mjson.h"

#include <stdio.h>

#include "mjson.h"
#include "pico_logger.h"

static int hex_digit(char c)
{
    if ((c >= '0') && (c <= '9')) return c - '0';
    if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    return -1;
}

static int32_t hex4(const char *s, uint32_t len)
{
    if (len < 4)
    {
        return -1;
    }

    int32_t value = 0;

    for (uint32_t i = 0; i < 4; ++i)
    {
        int digit = hex_digit(s[i]);

        if (digit < 0)
        {
            return -1;
        }

        value = (value << 4) | digit;
    }

    return value;
}

template <typename Out>
static void put_utf8(uint32_t cp, Out& out)
{
    if (cp < 0x80)
    {
        out((char)cp);
    }
    else if (cp < 0x800)
    {
        out((char)(0xC0 | (cp >> 6)));
        out((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out((char)(0xE0 | (cp >> 12)));
        out((char)(0x80 | ((cp >> 6) & 0x3F)));
        out((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out((char)(0xF0 | (cp >> 18)));
        out((char)(0x80 | ((cp >> 12) & 0x3F)));
        out((char)(0x80 | ((cp >> 6) & 0x3F)));
        out((char)(0x80 | (cp & 0x3F)));
    }
}

template <typename Out>
static bool unescape(const char *s, uint32_t len, Out out)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        if (s[i] != '\\')
        {
            out(s[i]);
            continue;
        }

        if (++i >= len)
        {
            return false;
        }

        switch (s[i])
        {
            case '"':  out('"');  break;
            case '\\': out('\\'); break;
            case '/':  out('/');  break;
            case 'b':  out('\b'); break;
            case 'f':  out('\f'); break;
            case 'n':  out('\n'); break;
            case 'r':  out('\r'); break;
            case 't':  out('\t'); break;
            case 'u':
            {
                int32_t cp = hex4(&s[i + 1], len - i - 1);

                if (cp < 0)
                {
                    return false;
                }

                i += 4;

                if ((cp >= 0xD800) && (cp <= 0xDBFF))
                {
                    int32_t low = ((i + 2 < len) && (s[i + 1] == '\\') && (s[i + 2] == 'u')) ? hex4(&s[i + 3], len - i - 3) : -1;

                    if ((low >= 0xDC00) && (low <= 0xDFFF))
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                    else
                    {
                        cp = 0xFFFD;
                    }
                }
                else if ((cp >= 0xDC00) && (cp <= 0xDFFF))
                {
                    cp = 0xFFFD;
                }

                put_utf8((uint32_t)cp, out);
                break;
            }
            default:
                return false;
        }
    }

    return true;
}

static uint32_t utf8_boundary(const char *text, uint32_t len)
{
    uint32_t start = len;

    while ((start > 0) && ((((uint8_t)text[start - 1]) & 0xC0) == 0x80))
    {
        --start;
    }

    if (start == 0)
    {
        return len;
    }

    uint8_t lead = (uint8_t)text[start - 1];
    uint32_t needed = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;

    return ((len - (start - 1)) < needed) ? (start - 1) : len;
}

static JsonType kind(const JsonView& value)
{
    if (value.length() == 0)
    {
        return JsonType::Missing;
    }

    switch (value.data()[0])
    {
        case '{': return JsonType::Object;
        case '[': return JsonType::Array;
        case '"': return JsonType::String;
        case 't':
        case 'f': return JsonType::Bool;
        case 'n': return JsonType::Null;
        default:  return JsonType::Number;
    }
}

bool JsonView::is_valid() const
{
    return (m_data != NULL) && (m_len > 0) && (mjson(m_data, (int)m_len, NULL, NULL) > 0);
}

JsonType JsonView::type(const char *path) const
{
    return kind(find(path));
}

JsonView JsonView::find(const char *path) const
{
    const char *token = NULL;
    int token_len = 0;

    if ((m_data == NULL) || (path == NULL) || (mjson_find(m_data, (int)m_len, path, &token, &token_len) == MJSON_TOK_INVALID))
    {
        return JsonView();
    }

    return JsonView(token, (uint32_t)token_len);
}

JsonView JsonView::member(const JsonView& key) const
{
    if (type() != JsonType::Object)
    {
        return JsonView();
    }

    for (const JsonEntry& entry : *this)
    {
        if ((entry.key.m_len == key.m_len) && (memcmp(entry.key.m_data, key.m_data, key.m_len) == 0))
        {
            return entry.value;
        }
    }

    return JsonView();
}

bool JsonView::get_string(const char *path, char *out, uint32_t size) const
{
    return copy_string(path, out, size, true);
}

bool JsonView::get_whole_string(const char *path, char *out, uint32_t size) const
{
    return copy_string(path, out, size, false);
}

bool JsonView::copy_string(const char *path, char *out, uint32_t size, bool allow_cut) const
{
    if ((out == NULL) || (size == 0))
    {
        return false;
    }

    out[0] = 0;

    JsonView value = find(path);
    JsonType type = kind(value);

    uint32_t len = 0;
    bool cut = false;

    auto copy = [&](char c)
    {
        if (len + 1 < size)
        {
            out[len++] = c;
        }
        else
        {
            cut = true;
        }
    };

    if (type == JsonType::String)
    {
        if (!unescape(value.m_data + 1, value.m_len - 2, copy))
        {
            out[0] = 0;
            return false;
        }
    }
    else if ((type == JsonType::Number) || (type == JsonType::Bool))
    {
        for (uint32_t i = 0; i < value.m_len; ++i)
        {
            copy(value.m_data[i]);
        }
    }
    else
    {
        return false;
    }

    if (cut && !allow_cut)
    {
        if (type == JsonType::String)
        {
            trace("JsonView::get_whole_string: %s is longer than %u bytes, ignored", path, (unsigned int)(size - 1));
        }

        out[0] = 0;
        return false;
    }

    out[cut ? utf8_boundary(out, len) : len] = 0;
    return true;
}

bool JsonView::get_int(const char *path, int64_t& out) const
{
    JsonView value = find(path);

    if (kind(value) != JsonType::Number)
    {
        return false;
    }

    const char *s = value.m_data;
    uint32_t len = value.m_len;
    uint32_t i = 0;
    bool negative = (s[0] == '-');

    if (negative)
    {
        ++i;
    }

    if (i >= len)
    {
        return false;
    }

    int64_t result = 0;

    for (; i < len; ++i)
    {
        if ((s[i] < '0') || (s[i] > '9'))
        {
            return false;   // a fraction or an exponent
        }

        int digit = s[i] - '0';

        if (result < (INT64_MIN + digit) / 10)
        {
            return false;
        }

        result = result * 10 - digit;
    }

    if (!negative)
    {
        if (result == INT64_MIN)
        {
            return false;
        }
        result = -result;
    }

    out = result;
    return true;
}

bool JsonView::get_uint(const char *path, uint32_t& out) const
{
    int64_t value = 0;

    if (!get_int(path, value) || (value < 0) || (value > (int64_t)UINT32_MAX))
    {
        return false;
    }

    out = (uint32_t)value;
    return true;
}

bool JsonView::get_number(const char *path, double& out) const
{
    if ((m_data == NULL) || (path == NULL))
    {
        return false;
    }

    return mjson_get_number(m_data, (int)m_len, path, &out) != 0;
}

bool JsonView::get_bool(const char *path, bool& out) const
{
    int value = 0;

    if ((m_data == NULL) || (path == NULL) || (mjson_get_bool(m_data, (int)m_len, path, &value) == 0))
    {
        return false;
    }

    out = (value != 0);
    return true;
}

bool JsonView::same_string(const char *path, const char *text) const
{
    JsonView value = find(path);
    JsonType type = kind(value);

    if ((type == JsonType::Missing) || (type == JsonType::Null))
    {
        return (text != NULL) && (text[0] == 0);
    }

    return value.equals(text);
}

bool JsonView::equals(const char *text) const
{
    return compare(text, false);
}

bool JsonView::starts_with(const char *prefix) const
{
    return compare(prefix, true);
}

bool JsonView::compare(const char *text, bool prefix) const
{
    if ((text == NULL) || (kind(*this) != JsonType::String) || (m_len < 2))
    {
        return false;
    }

    const char *at = text;
    bool same = true;

    bool ok = unescape(m_data + 1, m_len - 2, [&](char c)
    {
        if (same && (*at != 0) && (*at == c))
        {
            ++at;
        }
        else if ((*at != 0) || !prefix)
        {
            same = false;
        }
    });

    return ok && same && (*at == 0);
}

JsonIterator JsonView::begin() const
{
    JsonIterator it(m_data, m_len);
    it.advance();
    return it;
}

JsonIterator JsonView::end() const
{
    return JsonIterator(m_data, m_len);
}

void JsonIterator::advance()
{
    if (m_started && (m_offset == 0))
    {
        return;
    }

    int key_offset = 0;
    int key_len = 0;
    int value_offset = 0;
    int value_len = 0;
    int value_type = 0;

    m_offset = (m_data == NULL) ? 0 : mjson_next(m_data, (int)m_len, m_offset, &key_offset, &key_len, &value_offset, &value_len, &value_type);
    m_started = true;

    if (m_offset == 0)
    {
        m_entry = JsonEntry();
        return;
    }

    m_entry.key = (key_len > 0) ? JsonView(m_data + key_offset, (uint32_t)key_len) : JsonView();
    m_entry.value = JsonView(m_data + value_offset, (uint32_t)value_len);
}

static uint32_t escape_char(char c, char *out)
{
    static const char HEX[] = "0123456789abcdef";

    uint8_t byte = (uint8_t)c;
    char named = 0;

    switch (byte)
    {
        case '"':  named = '"';  break;
        case '\\': named = '\\'; break;
        case '\b': named = 'b';  break;
        case '\f': named = 'f';  break;
        case '\n': named = 'n';  break;
        case '\r': named = 'r';  break;
        case '\t': named = 't';  break;
        default:
            if (byte >= 0x20)
            {
                out[0] = c;
                return 1;
            }

            out[0] = '\\';
            out[1] = 'u';
            out[2] = '0';
            out[3] = '0';
            out[4] = HEX[byte >> 4];
            out[5] = HEX[byte & 0xF];
            return 6;
    }

    out[0] = '\\';
    out[1] = named;
    return 2;
}

JsonWriter::JsonWriter(char *buffer, uint32_t size)
    : m_buffer(buffer), m_size((buffer != NULL) ? size : 0)
{
    if (m_size > 0)
    {
        m_buffer[0] = 0;
    }
    else
    {
        m_failed = true;
    }
}

void JsonWriter::put(const char *data, uint32_t len)
{
    if (m_failed || (m_len + len > m_size - 1))
    {
        m_failed = true;
        return;
    }

    memcpy(&m_buffer[m_len], data, len);
    m_len += len;
    m_buffer[m_len] = 0;
}

// Bytes in the UTF-8 sequence at s, 0 if it is not a valid one: a stray
// continuation byte, a cut off sequence, an overlong form or a surrogate.
static uint32_t utf8_length(const uint8_t *s, uint32_t left)
{
    uint8_t lead = s[0];
    uint8_t low = 0x80, high = 0xBF;
    uint32_t length;

    if ((lead >= 0xC2) && (lead <= 0xDF))
    {
        length = 2;
    }
    else if ((lead >= 0xE0) && (lead <= 0xEF))
    {
        length = 3;
        low = (lead == 0xE0) ? 0xA0 : low;
        high = (lead == 0xED) ? 0x9F : high;
    }
    else if ((lead >= 0xF0) && (lead <= 0xF4))
    {
        length = 4;
        low = (lead == 0xF0) ? 0x90 : low;
        high = (lead == 0xF4) ? 0x8F : high;
    }
    else
    {
        return 0;
    }

    if ((left < length) || (s[1] < low) || (s[1] > high))
    {
        return 0;
    }

    for (uint32_t i = 2; i < length; ++i)
    {
        if ((s[i] & 0xC0) != 0x80)
        {
            return 0;
        }
    }

    return length;
}

// What the text at s[*at] is written as inside a JSON string, into out (up
// to 6 bytes), moving *at past it. Bytes that are not valid UTF-8 become
// U+FFFD: a server is free to refuse a body that is not UTF-8.
static uint32_t escape_at(const char *s, uint32_t len, uint32_t *at, char *out)
{
    uint8_t byte = (uint8_t)s[*at];

    if (byte < 0x80)
    {
        ++*at;
        return escape_char((char)byte, out);
    }

    uint32_t length = utf8_length((const uint8_t *)&s[*at], len - *at);

    if (length == 0)
    {
        ++*at;
        memcpy(out, "\\ufffd", 6);
        return 6;
    }

    memcpy(out, &s[*at], length);
    *at += length;
    return length;
}

void JsonWriter::put_escaped(const char *value, uint32_t len)
{
    put("\"", 1);

    for (uint32_t i = 0; i < len;)
    {
        char escaped[6];
        put(escaped, escape_at(value, len, &i, escaped));
    }

    put("\"", 1);
}

void JsonWriter::value_start(const char *key)
{
    JsonView token = m_key;
    m_key = JsonView();

    bool has_key = (key != NULL) || (token.length() > 0);

    if (m_depth == 0)
    {
        if (m_has_value || has_key)
        {
            m_failed = true;
        }

        m_has_value = true;
        return;
    }

    uint32_t bit = 1u << (m_depth - 1);
    bool in_array = (m_is_array & bit) != 0;

    if (in_array != !has_key)
    {
        m_failed = true;
    }

    if (m_has_items & bit)
    {
        put(",", 1);
    }

    m_has_items |= bit;

    if (key != NULL)
    {
        put_escaped(key, strlen(key));
        put(":", 1);
    }
    else if (has_key)
    {
        put(token.data(), token.length());
        put(":", 1);
    }
}

JsonWriter& JsonWriter::key(const JsonView& token)
{
    // Only a string token can be a key.
    if ((token.length() < 2) || (token.data()[0] != '"'))
    {
        m_failed = true;
        return *this;
    }

    m_key = token;
    return *this;
}

void JsonWriter::begin(const char *key, char open)
{
    value_start(key);

    if (m_depth >= MAX_DEPTH)
    {
        m_failed = true;
        return;
    }

    put(&open, 1);

    uint32_t bit = 1u << m_depth;

    m_has_items &= ~bit;
    m_is_array = (open == '[') ? (m_is_array | bit) : (m_is_array & ~bit);
    ++m_depth;
}

void JsonWriter::end(char close)
{
    if (m_depth == 0)
    {
        m_failed = true;
        return;
    }

    bool in_array = (m_is_array & (1u << (m_depth - 1))) != 0;

    if (in_array != (close == ']'))
    {
        m_failed = true;
    }

    --m_depth;
    put(&close, 1);
}

JsonWriter& JsonWriter::begin_object(const char *key) { begin(key, '{'); return *this; }
JsonWriter& JsonWriter::end_object() { end('}'); return *this; }
JsonWriter& JsonWriter::begin_array(const char *key) { begin(key, '['); return *this; }
JsonWriter& JsonWriter::end_array() { end(']'); return *this; }

JsonWriter& JsonWriter::add_string(const char *key, const char *value)
{
    if (value == NULL)
    {
        return add_null(key);
    }

    value_start(key);
    put_escaped(value, strlen(value));
    return *this;
}

JsonWriter& JsonWriter::add_fstring(const char *key, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    add_vstring(key, format, args);
    va_end(args);

    return *this;
}

JsonWriter& JsonWriter::add_vstring(const char *key, const char *format, va_list args)
{
    value_start(key);
    put("\"", 1);

    if (m_failed || (format == NULL))
    {
        m_failed = true;
        return *this;
    }

    uint32_t start = m_len;
    uint32_t room = m_size - 1 - start;
    int len = vsnprintf(&m_buffer[start], room + 1, format, args);

    if ((len < 0) || ((uint32_t)len >= room))
    {
        m_buffer[start] = 0;
        m_failed = true;
        return *this;
    }

    uint32_t end = start + room;
    uint32_t read = end - len;
    uint32_t write = start;

    memmove(&m_buffer[read], &m_buffer[start], len);

    while (read < end)
    {
        char escaped[6];
        uint32_t bytes = escape_at(m_buffer, end, &read, escaped);

        if (write + bytes > read)
        {
            m_buffer[start] = 0;
            m_failed = true;
            return *this;
        }

        memcpy(&m_buffer[write], escaped, bytes);
        write += bytes;
    }

    m_len = write;
    m_buffer[m_len] = 0;

    put("\"", 1);
    return *this;
}

JsonWriter& JsonWriter::add_int(const char *key, int64_t value)
{
    if (value >= 0)
    {
        return add_uint(key, (uint64_t)value);
    }

    uint64_t magnitude = (uint64_t)0 - (uint64_t)value;
    char digits[21];
    uint32_t n = 0;

    do
    {
        digits[sizeof(digits) - 1 - n++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0);

    digits[sizeof(digits) - 1 - n++] = '-';

    value_start(key);
    put(&digits[sizeof(digits) - n], n);
    return *this;
}

JsonWriter& JsonWriter::add_uint(const char *key, uint64_t value)
{
    char digits[20];
    uint32_t n = 0;

    do
    {
        digits[sizeof(digits) - 1 - n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0);

    value_start(key);
    put(&digits[sizeof(digits) - n], n);
    return *this;
}

int JsonWriter::print(const char *data, int len, void *self)
{
    ((JsonWriter *)self)->put(data, (uint32_t)len);
    return len;
}

JsonWriter& JsonWriter::add_number(const char *key, double value)
{
    value_start(key);
    mjson_print_dbl(print, this, value, 6);
    return *this;
}

JsonWriter& JsonWriter::add_bool(const char *key, bool value)
{
    value_start(key);
    put(value ? "true" : "false", value ? 4 : 5);
    return *this;
}

JsonWriter& JsonWriter::add_null(const char *key)
{
    value_start(key);
    put("null", 4);
    return *this;
}

JsonWriter& JsonWriter::add_raw(const char *key, const JsonView& json)
{
    if (json.length() == 0)
    {
        return add_null(key);
    }

    value_start(key);
    put(json.data(), json.length());
    return *this;
}
