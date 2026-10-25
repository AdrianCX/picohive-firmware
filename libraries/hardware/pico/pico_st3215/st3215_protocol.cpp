/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "st3215_protocol.h"

static uint8_t checksum(const uint8_t *packet, size_t len)
{
    // Everything between the FF FF header and the checksum itself.
    uint8_t sum = 0;
    for (size_t i = 2; i < len - 1; ++i)
    {
        sum += packet[i];
    }
    return ~sum;
}

size_t st3215_build_packet(uint8_t id, uint8_t instruction, const uint8_t *params, uint8_t count, uint8_t *out, size_t out_size)
{
    size_t len = count + 6;
    if ((count > ST3215_MAX_PARAMS) || (len > out_size))
    {
        return 0;
    }

    out[0] = 0xFF;
    out[1] = 0xFF;
    out[2] = id;
    out[3] = count + 2;
    out[4] = instruction;
    if (count > 0)
    {
        memcpy(out + 5, params, count);
    }
    out[len - 1] = checksum(out, len);
    return len;
}

bool st3215_parse_status(const uint8_t *data, size_t len, ST3215Status& status)
{
    if ((len < 6) || (data[0] != 0xFF) || (data[1] != 0xFF) || (data[3] < 2) || ((size_t)data[3] + 4 != len))
    {
        return false;
    }

    if (data[len - 1] != checksum(data, len))
    {
        return false;
    }

    status.id = data[2];
    status.error = data[4];
    status.count = data[3] - 2;
    status.params = data + 5;
    return true;
}

int32_t st3215_signed(uint16_t value, uint8_t sign_bit)
{
    uint16_t sign = 1 << sign_bit;
    int32_t magnitude = value & (sign - 1);
    return (value & sign) ? -magnitude : magnitude;
}

const char *st3215_error_text(uint8_t error, char *out, size_t out_size)
{
    static const struct { uint8_t bit; const char *name; } names[] = {
        { ST3215_ERROR_VOLTAGE, "voltage" },
        { ST3215_ERROR_SENSOR, "sensor" },
        { ST3215_ERROR_TEMPERATURE, "temperature" },
        { ST3215_ERROR_CURRENT, "current" },
        { ST3215_ERROR_OVERLOAD, "overload" },
    };

    size_t used = 0;
    out[0] = 0;
    for (const auto& name : names)
    {
        if ((error & name.bit) && (used < out_size))
        {
            used += snprintf(out + used, out_size - used, "%s%s", used ? " " : "", name.name);
        }
    }

    if ((used == 0) && (error != 0))
    {
        snprintf(out, out_size, "0x%02x", error);
    }
    return out;
}

void ST3215Range::add(uint16_t position)
{
    if (empty)
    {
        min = max = last = position;
        empty = false;
        return;
    }

    // Nobody turns a joint half a turn between two reads: a jump that big is
    // the reading going over the 4095/0 edge.
    int32_t jump = (int32_t)position - (int32_t)last;
    if ((jump > ST3215_STEPS / 2) || (jump < -ST3215_STEPS / 2))
    {
        wrapped = true;
    }

    if (position < min)
    {
        min = position;
    }
    if (position > max)
    {
        max = position;
    }
    last = position;
}

float st3215_to_degrees(uint16_t position)
{
    return ((int32_t)position - ST3215_MIDDLE) * 360.0f / ST3215_STEPS;
}

uint16_t st3215_from_degrees(float degrees)
{
    float steps = ST3215_MIDDLE + degrees * ST3215_STEPS / 360.0f;
    if (steps <= 0)
    {
        return 0;
    }
    if (steps >= ST3215_MAX_POSITION)
    {
        return ST3215_MAX_POSITION;
    }
    return (uint16_t)(steps + 0.5f);
}

ST3215Found st3215_finder_feed(ST3215Finder& finder, uint8_t byte, uint8_t id, uint8_t reply_count,
                               const uint8_t *sent, size_t sent_len)
{
    if (finder.have >= sizeof(finder.window))
    {
        finder.have = 0;
    }

    finder.window[finder.have++] = byte;

    while (finder.have >= 4)
    {
        size_t whole = (size_t)finder.window[3] + 4;

        bool header = (finder.window[0] == 0xFF) && (finder.window[1] == 0xFF)
                   && (finder.window[2] <= ST3215_BROADCAST_ID)
                   && (finder.window[3] >= 2) && (whole <= sizeof(finder.window));

        if (header)
        {
            if (finder.have < whole)
            {
                return ST3215Found::More;       // a header, waiting on its body
            }

            if ((sent != NULL) && (whole == sent_len) && (memcmp(finder.window, sent, sent_len) == 0))
            {
                finder.echo_intact = true;
                finder.have = 0;
                return ST3215Found::More;
            }

            ST3215Status status;

            if (st3215_parse_status(finder.window, whole, status) && (status.id == id)
                && (status.count == reply_count))
            {
                return ST3215Found::Answer;
            }
        }

        memmove(finder.window, finder.window + 1, --finder.have);
        ++finder.skipped;
    }

    return ST3215Found::More;
}

uint32_t st3215_baud_of(uint8_t code)
{
    static const uint32_t rates[] = { 1000000, 500000, 250000, 128000, 115200, 76800, 57600, 38400 };

    return (code < (sizeof(rates) / sizeof(rates[0]))) ? rates[code] : 0;
}
