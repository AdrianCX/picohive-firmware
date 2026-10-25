/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// ST3215Bus on linux - simulated servos
//   - a move (goal position, speed) takes as long as hardware
//   - new servos start calibrated at 1024..3072 (-90..90 degrees)
//   - 128 into torque enable sets the current position as middle
//   - the EEPROM part (id, limits, offset, ...) is kept in <data dir>/st3215_sim.bin
//   - voltage 12.0V, 30C, no load and no current

#include <stdio.h>
#include <string.h>

#include <string>

#include "os_linux.h"
#include "os_platform.h"
#include "pico_logger.h"
#include "st3215.h"

#ifndef ST3215_SIM_SERVOS
#define ST3215_SIM_SERVOS 6
#endif

namespace
{

const uint8_t EEPROM_SIZE = 40;
const uint8_t REGISTERS = 71;
const char *SIM_FILE = "st3215_sim.bin";

struct SimServo
{
    bool present = false;
    uint8_t reg[REGISTERS] = {};
    uint16_t start = ST3215_MIDDLE;
    uint16_t goal = ST3215_MIDDLE;
    uint16_t speed = ST3215_MAX_SPEED;
    uint64_t start_us = 0;
};

SimServo servos[ST3215_BROADCAST_ID];
bool loaded = false;

void put_u16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value & 0xFF);
    at[1] = (uint8_t)(value >> 8);
}

uint16_t get_u16(const uint8_t *at)
{
    return (uint16_t)(at[0] | (at[1] << 8));
}

void factory(SimServo& servo, uint8_t id)
{
    servo = SimServo();
    servo.present = true;
    servo.reg[ST3215_REG_FIRMWARE] = 3;
    servo.reg[ST3215_REG_FIRMWARE + 1] = 10;
    servo.reg[ST3215_REG_ID] = id;
    servo.reg[ST3215_REG_RESPONSE_LEVEL] = 1;
    put_u16(&servo.reg[ST3215_REG_MIN_ANGLE], 1024);
    put_u16(&servo.reg[ST3215_REG_MAX_ANGLE], 3072);
    servo.reg[ST3215_REG_TEMP_LIMIT] = 70;
    servo.reg[ST3215_REG_MAX_VOLTAGE] = 140;
    servo.reg[ST3215_REG_MIN_VOLTAGE] = 40;
    put_u16(&servo.reg[ST3215_REG_MAX_TORQUE], 1000);
    servo.reg[ST3215_REG_LOCK] = 1;
}

std::string sim_path()
{
    return std::string(os_data_dir()) + "/" + SIM_FILE;
}

void save()
{
    FILE *f = fopen(sim_path().c_str(), "wb");
    if (f == NULL)
    {
        trace("st3215 sim: cannot write %s", sim_path().c_str());
        return;
    }
    for (const SimServo& servo : servos)
    {
        uint8_t present = servo.present ? 1 : 0;
        fwrite(&present, 1, 1, f);
        fwrite(servo.reg, 1, EEPROM_SIZE, f);
    }
    fclose(f);
}

void load()
{
    if (loaded)
    {
        return;
    }
    loaded = true;

    FILE *f = fopen(sim_path().c_str(), "rb");
    bool ok = (f != NULL);

    for (uint32_t id = 0; ok && (id < ST3215_BROADCAST_ID); ++id)
    {
        uint8_t present = 0;
        SimServo& servo = servos[id];
        servo = SimServo();
        ok = (fread(&present, 1, 1, f) == 1) && (fread(servo.reg, 1, EEPROM_SIZE, f) == EEPROM_SIZE);
        servo.present = (present != 0);
        servo.reg[ST3215_REG_LOCK] = 1;
    }
    if (f != NULL)
    {
        fclose(f);
    }

    if (ok)
    {
        trace("st3215 sim: servos as they were left, from %s", sim_path().c_str());
        return;
    }

    for (uint32_t id = 0; id < ST3215_BROADCAST_ID; ++id)
    {
        servos[id] = SimServo();
    }
    for (uint32_t id = 1; (id <= ST3215_SIM_SERVOS) && (id < ST3215_BROADCAST_ID); ++id)
    {
        factory(servos[id], (uint8_t)id);
    }
    trace("st3215 sim: %d new servos, ids 1..%d, calibrated to 1024..3072", ST3215_SIM_SERVOS, ST3215_SIM_SERVOS);
    save();
}

uint16_t position_of(const SimServo& servo, uint64_t now_us, bool& moving)
{
    uint64_t travelled = ((now_us - servo.start_us) * servo.speed) / 1000000ULL;
    uint32_t distance = (servo.goal > servo.start) ? (servo.goal - servo.start) : (servo.start - servo.goal);

    moving = travelled < distance;
    if (!moving)
    {
        return servo.goal;
    }
    return (servo.goal > servo.start) ? (uint16_t)(servo.start + travelled) : (uint16_t)(servo.start - travelled);
}

void hold_here(SimServo& servo, uint64_t now_us)
{
    bool moving;
    servo.start = position_of(servo, now_us, moving);
    servo.goal = servo.start;
    servo.start_us = now_us;
}

SimServo *find(uint8_t id)
{
    load();
    return ((id < ST3215_BROADCAST_ID) && servos[id].present) ? &servos[id] : NULL;
}

}

bool ST3215Bus::begin(const ST3215BusConfig& config)
{
    m_config = config;
    m_started = true;
    load();
    trace("ST3215Bus::begin: simulated bus, no uart on linux");
    return true;
}

bool ST3215Bus::line_test(ST3215Line& line)
{
    line.rx_idle_high = true;
    line.pulled_low = true;
    line.released_high = true;
    return true;
}

bool ST3215Bus::set_baudrate(uint32_t baudrate)
{
    m_config.baudrate = baudrate;
    return true;
}

bool ST3215Bus::ping(uint8_t id)
{
    return find(id) != NULL;
}

bool ST3215Bus::probe(uint8_t id)
{
    return ping(id);
}

bool ST3215Bus::read(uint8_t id, uint8_t reg, uint8_t *data, uint8_t count)
{
    SimServo *servo = find(id);
    if ((servo == NULL) || (reg + count > REGISTERS))
    {
        return false;
    }

    bool moving = false;
    uint16_t position = position_of(*servo, time_us_64(), moving);

    put_u16(&servo->reg[ST3215_REG_PRESENT_POSITION], position);
    put_u16(&servo->reg[ST3215_REG_PRESENT_POSITION + 2],
            moving ? (uint16_t)(servo->speed | ((servo->goal < servo->start) ? 0x8000 : 0)) : 0);
    servo->reg[ST3215_REG_PRESENT_VOLTAGE] = 120;
    servo->reg[ST3215_REG_PRESENT_TEMP] = 30;
    servo->reg[ST3215_REG_MOVING] = moving ? 1 : 0;

    memcpy(data, &servo->reg[reg], count);
    m_last_error = 0;
    return true;
}

bool ST3215Bus::write(uint8_t id, uint8_t reg, const uint8_t *data, uint8_t count)
{
    SimServo *servo = find(id);
    if ((servo == NULL) || (reg + count > REGISTERS))
    {
        return false;
    }

    uint64_t now_us = time_us_64();
    bool unlocked = (servo->reg[ST3215_REG_LOCK] == 0);

    for (uint8_t i = 0; i < count; ++i)
    {
        uint8_t r = reg + i;

        // The EEPROM takes writes only while unlocked
        if ((r < EEPROM_SIZE) && !unlocked)
        {
            continue;
        }
        if ((r == ST3215_REG_TORQUE_ENABLE) && (data[i] == ST3215_TORQUE_MIDDLE))
        {
            hold_here(*servo, now_us);
            put_u16(&servo->reg[ST3215_REG_OFFSET], 0);
            servo->start = servo->goal = ST3215_MIDDLE;
            servo->reg[ST3215_REG_TORQUE_ENABLE] = 0;
            continue;
        }
        servo->reg[r] = data[i];
    }

    if ((reg <= ST3215_REG_TORQUE_ENABLE) && (reg + count > ST3215_REG_TORQUE_ENABLE) &&
        (servo->reg[ST3215_REG_TORQUE_ENABLE] == 0))
    {
        hold_here(*servo, now_us);
    }

    if ((reg <= ST3215_REG_GOAL_POSITION + 1) && (reg + count > ST3215_REG_GOAL_POSITION))
    {
        hold_here(*servo, now_us);
        uint16_t speed = get_u16(&servo->reg[ST3215_REG_GOAL_POSITION + 4]);
        servo->goal = get_u16(&servo->reg[ST3215_REG_GOAL_POSITION]);
        servo->speed = ((speed == 0) || (speed > ST3215_MAX_SPEED)) ? ST3215_MAX_SPEED : speed;
        if (servo->goal > ST3215_MAX_POSITION)
        {
            servo->goal = ST3215_MAX_POSITION;
        }
        servo->reg[ST3215_REG_TORQUE_ENABLE] = 1;
    }

    uint8_t new_id = servo->reg[ST3215_REG_ID];
    if ((new_id != id) && (new_id < ST3215_BROADCAST_ID))
    {
        servos[new_id] = *servo;
        *servo = SimServo();
        trace("st3215 sim: id %d is now id %d", id, new_id);
    }

    if (unlocked && (reg < EEPROM_SIZE))
    {
        save();
    }

    m_last_error = 0;
    return true;
}
