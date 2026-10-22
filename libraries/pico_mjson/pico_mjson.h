/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

enum class JsonType : uint8_t
{
    Missing,
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

class JsonIterator;

class JsonView
{
public:
    JsonView() = default;
    JsonView(const char *data, uint32_t len) : m_data(data), m_len(len) {}
    explicit JsonView(const char *text) : m_data(text), m_len((text != NULL) ? strlen(text) : 0) {}

    const char *data() const { return m_data; }
    uint32_t length() const { return m_len; }

    bool is_valid() const;

    JsonType type(const char *path = "$") const;
    bool has(const char *path) const { return type(path) != JsonType::Missing; }

    JsonView find(const char *path) const;
    JsonView member(const JsonView& key) const;

    bool get_string(const char *path, char *out, uint32_t size) const;
    bool get_whole_string(const char *path, char *out, uint32_t size) const;
    bool get_int(const char *path, int64_t& out) const;
    bool get_uint(const char *path, uint32_t& out) const;
    bool get_number(const char *path, double& out) const;
    bool get_bool(const char *path, bool& out) const;

    bool equals(const char *text) const;
    bool starts_with(const char *prefix) const;

    bool same_string(const char *path, const char *text) const;

    JsonIterator begin() const;
    JsonIterator end() const;

private:
    bool copy_string(const char *path, char *out, uint32_t size, bool allow_cut) const;
    bool compare(const char *text, bool prefix) const;

    const char *m_data = NULL;
    uint32_t m_len = 0;
};

struct JsonEntry
{
    JsonView key;
    JsonView value;
};

class JsonIterator
{
public:
    const JsonEntry& operator*() const { return m_entry; }
    const JsonEntry *operator->() const { return &m_entry; }

    JsonIterator& operator++() { advance(); return *this; }
    bool operator==(const JsonIterator& other) const { return m_offset == other.m_offset; }
    bool operator!=(const JsonIterator& other) const { return m_offset != other.m_offset; }

private:
    friend class JsonView;

    JsonIterator(const char *data, uint32_t len) : m_data(data), m_len(len) {}
    void advance();

    const char *m_data = NULL;
    uint32_t m_len = 0;
    int m_offset = 0;
    bool m_started = false;
    JsonEntry m_entry;
};

class JsonWriter
{
public:
    JsonWriter(char *buffer, uint32_t size);

    JsonWriter& begin_object(const char *key = NULL);
    JsonWriter& end_object();
    JsonWriter& begin_array(const char *key = NULL);
    JsonWriter& end_array();

    JsonWriter& add_string(const char *key, const char *value);
    JsonWriter& add_fstring(const char *key, const char *format, ...) __attribute__((format(printf, 3, 4)));
    JsonWriter& add_vstring(const char *key, const char *format, va_list args);
    JsonWriter& add_int(const char *key, int64_t value);
    JsonWriter& add_uint(const char *key, uint64_t value);
    JsonWriter& add_number(const char *key, double value);
    JsonWriter& add_bool(const char *key, bool value);
    JsonWriter& add_null(const char *key);
    JsonWriter& add_raw(const char *key, const JsonView& json);

    JsonWriter& key(const JsonView& token);

    bool ok() const { return !m_failed && (m_depth == 0); }

    const char *c_str() const { return m_buffer; }
    uint32_t length() const { return m_len; }

private:
    void value_start(const char *key);
    void put(const char *data, uint32_t len);
    void put_escaped(const char *value, uint32_t len);
    void begin(const char *key, char open);
    void end(char close);

    static int print(const char *data, int len, void *self);

    static const uint32_t MAX_DEPTH = 32;

    char *m_buffer;
    uint32_t m_size;
    uint32_t m_len = 0;
    bool m_failed = false;
    bool m_has_value = false;
    JsonView m_key;

    uint32_t m_depth = 0;
    uint32_t m_has_items = 0;
    uint32_t m_is_array = 0;
};
