/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#define ST3215_BROADCAST_ID     0xFE

#define ST3215_PING             0x01
#define ST3215_READ             0x02
#define ST3215_WRITE            0x03

#define ST3215_MAX_PARAMS       32
#define ST3215_MAX_PACKET       (ST3215_MAX_PARAMS + 6)

// One full turn: positions run 0..4095, the middle is 2048.
#define ST3215_STEPS            4096
#define ST3215_MAX_POSITION     4095
#define ST3215_MIDDLE           2048

#define ST3215_VOLTAGE_FLOOR    40

#define ST3215_MAX_SPEED        3400

// Memory table. EEPROM entries only take a write while LOCK is 0, and keep it over a power cycle.
#define ST3215_REG_FIRMWARE         0       // EEPROM, 2 bytes: major, minor
#define ST3215_REG_ID               5       // EEPROM
#define ST3215_REG_BAUD             6       // EEPROM, a code not a number: see st3215_baud_of
#define ST3215_REG_RETURN_DELAY     7       // EEPROM, 2us per unit, before it answers
#define ST3215_REG_RESPONSE_LEVEL   8       // EEPROM, 0 answers reads only, 1 answers everything
#define ST3215_REG_MIN_ANGLE        9       // EEPROM, 2 bytes
#define ST3215_REG_MAX_ANGLE        11      // EEPROM, 2 bytes
#define ST3215_REG_TEMP_LIMIT       13      // EEPROM, C
#define ST3215_REG_MAX_VOLTAGE      14      // EEPROM, 0.1V
#define ST3215_REG_MIN_VOLTAGE      15      // EEPROM, 0.1V - under this it faults rather than moves
#define ST3215_REG_MAX_TORQUE       16      // EEPROM, 2 bytes, 0..1000
#define ST3215_REG_OFFSET           31      // EEPROM, 2 bytes, where its middle was moved to
#define ST3215_REG_MODE             33      // EEPROM, 0 is position control
#define ST3215_REG_TORQUE_ENABLE    40      // 0 off, 1 on, 128 makes here the middle
#define ST3215_REG_ACCELERATION     41      // 100 steps/s^2 per unit, 0 none
#define ST3215_REG_GOAL_POSITION    42      // 2 bytes, then 2 of time, 2 of speed
#define ST3215_REG_TORQUE_LIMIT     48      // 2 bytes, 0..1000 of the maximum
#define ST3215_REG_LOCK             55      // 0 lets EEPROM be written, 1 locks it
#define ST3215_REG_PRESENT_POSITION 56      // 2 bytes, the start of the 15 byte state
#define ST3215_REG_PRESENT_VOLTAGE  62      // 0.1V, what the servo thinks it is being fed
#define ST3215_REG_PRESENT_TEMP     63      // C
#define ST3215_REG_MOVING           66

#define ST3215_TORQUE_MIDDLE        128

#define ST3215_ERROR_VOLTAGE        0x01
#define ST3215_ERROR_SENSOR         0x02
#define ST3215_ERROR_TEMPERATURE    0x04
#define ST3215_ERROR_CURRENT        0x08
#define ST3215_ERROR_OVERLOAD       0x20

// Writes a packet into out and returns its size, 0 if params do not fit.
size_t st3215_build_packet(uint8_t id, uint8_t instruction, const uint8_t *params, uint8_t count, uint8_t *out, size_t out_size);

struct ST3215Status
{
    uint8_t id = 0;
    uint8_t error = 0;
    const uint8_t *params = NULL;
    uint8_t count = 0;
};

bool st3215_parse_status(const uint8_t *data, size_t len, ST3215Status& status);

struct ST3215Finder
{
    uint8_t window[ST3215_MAX_PACKET] = {};
    uint8_t have = 0;
    uint32_t skipped = 0;
    bool echo_intact = false;
};

enum class ST3215Found
{
    More,
    Answer,
};

ST3215Found st3215_finder_feed(ST3215Finder& finder, uint8_t byte, uint8_t id, uint8_t reply_count, const uint8_t *sent, size_t sent_len);
int32_t st3215_signed(uint16_t value, uint8_t sign_bit);
uint32_t st3215_baud_of(uint8_t code);

const char *st3215_error_text(uint8_t error, char *out, size_t out_size);

struct ST3215Range
{
    uint16_t min = 0;
    uint16_t max = 0;
    uint16_t last = 0;
    bool empty = true;
    bool wrapped = false;

    void reset() { *this = ST3215Range(); }
    void add(uint16_t position);
};

float st3215_to_degrees(uint16_t position);
uint16_t st3215_from_degrees(float degrees);
