/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "st3215.h"
#include "pico_logger.h"

bool ST3215Bus::read_u8(uint8_t id, uint8_t reg, uint8_t& value)
{
    return read(id, reg, &value, 1);
}

bool ST3215Bus::read_u16(uint8_t id, uint8_t reg, uint16_t& value)
{
    uint8_t data[2];
    if (!read(id, reg, data, 2))
    {
        return false;
    }
    value = data[0] | (data[1] << 8);
    return true;
}

bool ST3215Bus::write_u8(uint8_t id, uint8_t reg, uint8_t value)
{
    return write(id, reg, &value, 1);
}

bool ST3215Bus::write_u16(uint8_t id, uint8_t reg, uint16_t value)
{
    uint8_t data[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    return write(id, reg, data, 2);
}

bool ST3215::read_settings(ST3215Settings& settings)
{
    // Registers 0..17 in one go: firmware, id, baudrate, return delay, response level, angle limits, temperature, voltage, torque
    uint8_t block[18];

    if (!m_bus.read(m_id, ST3215_REG_FIRMWARE, block, sizeof(block)))
    {
        return false;
    }

    settings.firmware_major = block[ST3215_REG_FIRMWARE];
    settings.firmware_minor = block[1];
    settings.id = block[ST3215_REG_ID];
    settings.baud_code = block[ST3215_REG_BAUD];
    settings.return_delay = block[ST3215_REG_RETURN_DELAY];
    settings.response_level = block[ST3215_REG_RESPONSE_LEVEL];
    settings.min_angle = (uint16_t)(block[ST3215_REG_MIN_ANGLE] | (block[ST3215_REG_MIN_ANGLE + 1] << 8));
    settings.max_angle = (uint16_t)(block[ST3215_REG_MAX_ANGLE] | (block[ST3215_REG_MAX_ANGLE + 1] << 8));
    settings.temp_limit = block[ST3215_REG_TEMP_LIMIT];
    settings.max_voltage = block[ST3215_REG_MAX_VOLTAGE];
    settings.min_voltage = block[ST3215_REG_MIN_VOLTAGE];
    settings.max_torque = (uint16_t)(block[ST3215_REG_MAX_TORQUE] | (block[ST3215_REG_MAX_TORQUE + 1] << 8));

    uint8_t tail[3];

    if (m_bus.read(m_id, ST3215_REG_OFFSET, tail, sizeof(tail)))
    {
        settings.offset = (uint16_t)(tail[0] | (tail[1] << 8));
        settings.mode = tail[2];
    }

    uint8_t now[2];

    if (m_bus.read(m_id, ST3215_REG_PRESENT_VOLTAGE, now, sizeof(now)))
    {
        settings.voltage = now[0];
        settings.temperature = now[1];

        m_volts = now[0];
        m_temperature = now[1];
    }

    return true;
}

bool ST3215::begin()
{
    if (!m_bus.ping(m_id))
    {
        return false;
    }

    ST3215Settings settings;

    if (read_settings(settings))
    {
        uint32_t baud = st3215_baud_of(settings.baud_code);

        trace("st3215: id %d: firmware %d.%d, id register %d, baud %u (code %d)%s, return delay %dus, "
              "response level %d, mode %d, offset %d, angle limits %d..%d, temp limit %dC, "
              "voltage limits %d.%dV..%d.%dV, max torque %d, now %d.%dV %dC",
              m_id, settings.firmware_major, settings.firmware_minor, settings.id,
              (unsigned int)baud, settings.baud_code, (baud == 0) ? " - unknown code" : "",
              settings.return_delay * 2, settings.response_level, settings.mode,
              (int)st3215_signed(settings.offset, 11),      // sign in bit 11, not two's complement
              settings.min_angle, settings.max_angle, settings.temp_limit,
              settings.min_voltage / 10, settings.min_voltage % 10,
              settings.max_voltage / 10, settings.max_voltage % 10,
              settings.max_torque,
              settings.voltage / 10, settings.voltage % 10, settings.temperature);

        if ((settings.voltage > 0) && (settings.voltage < ST3215_VOLTAGE_FLOOR))
        {
            trace("st3215: id %d: fed %d.%dV, under the %d.%dV an ST3215 is rated to work at%s. Its data line "
                  "comes off that same rail, so the bus gets weaker with it",
                  m_id, settings.voltage / 10, settings.voltage % 10,
                  ST3215_VOLTAGE_FLOOR / 10, ST3215_VOLTAGE_FLOOR % 10,
                  (settings.min_voltage < ST3215_VOLTAGE_FLOOR)
                      ? " - and its own minimum is set below that too, so it will limp rather than complain"
                      : "");
        }
    }

    uint8_t mode;
    if (!m_bus.read_u8(m_id, ST3215_REG_MODE, mode))
    {
        return false;
    }
    if (mode != 0)
    {
        trace("st3215: id %d is in mode %d, not position control (0); not using it", m_id, mode);
        return false;
    }

    uint8_t limits[4];
    if (!m_bus.read(m_id, ST3215_REG_MIN_ANGLE, limits, sizeof(limits)))
    {
        return false;
    }
    m_min = limits[0] | (limits[1] << 8);
    m_max = limits[2] | (limits[3] << 8);

    uint16_t position = 0;
    read_position(position);

    trace("st3215: id %d at %d, limits %d..%d%s", m_id, position, m_min, m_max,
          is_calibrated() ? "" : " - not calibrated, moves refused until it is");
    return true;
}

void ST3215::set_default_motion(uint16_t speed, uint8_t acceleration)
{
    m_speed = speed;
    m_acceleration = acceleration;
}

bool ST3215::can_move(const char *what)
{
    if (m_calibrating)
    {
        trace("st3215: id %d: %s refused, calibrating", m_id, what);
        return false;
    }
    if (!is_calibrated())
    {
        trace("st3215: id %d: %s refused, not calibrated", m_id, what);
        return false;
    }
    return true;
}

bool ST3215::move_to(uint16_t position)
{
    return move_to(position, m_speed, m_acceleration);
}

bool ST3215::move_to(uint16_t position, uint16_t speed, uint8_t acceleration)
{
    if (!can_move("move"))
    {
        return false;
    }

    uint16_t goal = position;
    if (goal < m_min)
    {
        goal = m_min;
    }
    if (goal > m_max)
    {
        goal = m_max;
    }
    if (goal != position)
    {
        trace("st3215: id %d: %d is outside %d..%d, going to %d", m_id, position, m_min, m_max, goal);
    }

    // 0 means flat out to the servo - never ask for that by accident.
    if (speed == 0)
    {
        speed = 1;
    }
    if (speed > ST3215_MAX_SPEED)
    {
        speed = ST3215_MAX_SPEED;
    }

    if (!m_torque_on && !set_torque(true))
    {
        return false;
    }

    // acceleration, goal position, goal time (unused), goal speed.
    uint8_t data[7] = {
        acceleration,
        (uint8_t)(goal & 0xFF), (uint8_t)(goal >> 8),
        0, 0,
        (uint8_t)(speed & 0xFF), (uint8_t)(speed >> 8),
    };

    if (!m_bus.write(m_id, ST3215_REG_ACCELERATION, data, sizeof(data)))
    {
        return false;
    }

    m_goal = goal;
    m_has_goal = true;
    return true;
}

bool ST3215::move_to_degrees(float degrees)
{
    return move_to(st3215_from_degrees(degrees), m_speed, m_acceleration);
}

bool ST3215::move_to_degrees(float degrees, uint16_t speed, uint8_t acceleration)
{
    return move_to(st3215_from_degrees(degrees), speed, acceleration);
}

bool ST3215::move_by(int16_t steps)
{
    if (!can_move("move_by"))
    {
        return false;
    }

    uint16_t position;
    if (!read_position(position))
    {
        return false;
    }

    int32_t goal = (int32_t)position + steps;
    if (goal < 0)
    {
        goal = 0;
    }
    if (goal > ST3215_MAX_POSITION)
    {
        goal = ST3215_MAX_POSITION;
    }
    return move_to((uint16_t)goal);
}

bool ST3215::stop()
{
    if (!can_move("stop"))
    {
        return false;
    }

    uint16_t position;
    if (!read_position(position))
    {
        return false;
    }
    return move_to(position);
}

bool ST3215::set_torque(bool on)
{
    if (!m_bus.write_u8(m_id, ST3215_REG_TORQUE_ENABLE, on ? 1 : 0))
    {
        return false;
    }
    m_torque_on = on;
    return true;
}

bool ST3215::set_torque_limit(uint16_t limit)
{
    if (limit > 1000)
    {
        limit = 1000;
    }
    return m_bus.write_u16(m_id, ST3215_REG_TORQUE_LIMIT, limit);
}

bool ST3215::read_position(uint16_t& position)
{
    return m_bus.read_u16(m_id, ST3215_REG_PRESENT_POSITION, position);
}

bool ST3215::read_degrees(float& degrees)
{
    uint16_t position;
    if (!read_position(position))
    {
        return false;
    }
    degrees = st3215_to_degrees(position);
    return true;
}

bool ST3215::is_moving(bool& moving)
{
    uint8_t value;
    if (!m_bus.read_u8(m_id, ST3215_REG_MOVING, value))
    {
        return false;
    }
    moving = value != 0;
    return true;
}

bool ST3215::read_state(ST3215State& state)
{
    // 56 position, 58 speed, 60 load, 62 voltage, 63 temperature, 64-65 unused, 66 moving, 67-68 unused, 69 current.
    uint8_t data[15];
    if (!m_bus.read(m_id, ST3215_REG_PRESENT_POSITION, data, sizeof(data)))
    {
        return false;
    }

    state.position = data[0] | (data[1] << 8);
    state.speed = st3215_signed(data[2] | (data[3] << 8), 15);
    state.load = st3215_signed(data[4] | (data[5] << 8), 10);
    state.voltage = data[6];
    state.temperature = data[7];
    state.moving = data[10] != 0;
    state.current = st3215_signed(data[13] | (data[14] << 8), 15);
    return true;
}

bool ST3215::unlocked_write(uint8_t reg, const uint8_t *data, uint8_t count)
{
    if (!m_bus.write_u8(m_id, ST3215_REG_LOCK, 0))
    {
        return false;
    }

    bool ok = m_bus.write(m_id, reg, data, count);

    if (!m_bus.write_u8(m_id, ST3215_REG_LOCK, 1))
    {
        return false;
    }
    return ok;
}

bool ST3215::set_limits(uint16_t min, uint16_t max)
{
    if ((min >= max) || (max > ST3215_MAX_POSITION))
    {
        trace("st3215: id %d: limits %d..%d make no sense", m_id, min, max);
        return false;
    }

    uint8_t data[4] = {
        (uint8_t)(min & 0xFF), (uint8_t)(min >> 8),
        (uint8_t)(max & 0xFF), (uint8_t)(max >> 8),
    };
    if (!unlocked_write(ST3215_REG_MIN_ANGLE, data, sizeof(data)))
    {
        return false;
    }

    m_min = min;
    m_max = max;
    trace("st3215: id %d: limits %d..%d (%.1f..%.1f degrees) saved", m_id, min, max,
          (double)st3215_to_degrees(min), (double)st3215_to_degrees(max));
    return true;
}

bool ST3215::clear_limits()
{
    return set_limits(0, ST3215_MAX_POSITION);
}

bool ST3215::calibrate_start()
{
    m_range.reset();
    m_calibrating = false;

    if (!set_torque(false))
    {
        return false;
    }

    m_calibrating = true;
    trace("st3215: id %d: calibrating, torque off - move the joint gently to both ends", m_id);
    return calibrate_update();
}

bool ST3215::calibrate_update()
{
    if (!m_calibrating)
    {
        return false;
    }

    uint16_t position;
    if (!read_position(position))
    {
        return false;
    }

    bool was_wrapped = m_range.wrapped;
    m_range.add(position);
    if (m_range.wrapped && !was_wrapped)
    {
        trace("st3215: id %d: the travel crosses 4095/0 - finish will fail, see set_middle_here()", m_id);
    }
    return true;
}

bool ST3215::calibrate_finish(uint16_t margin)
{
    if (!m_calibrating)
    {
        trace("st3215: id %d: calibrate_finish without calibrate_start", m_id);
        return false;
    }
    m_calibrating = false;

    if (m_range.empty)
    {
        trace("st3215: id %d: calibration saw no positions", m_id);
        return false;
    }

    if (m_range.wrapped)
    {
        trace("st3215: id %d: calibration crossed 4095/0; put the joint mid travel, set_middle_here(), calibrate again", m_id);
        return false;
    }

    if (m_range.max - m_range.min < 2 * margin + 10)
    {
        trace("st3215: id %d: calibration saw only %d..%d, too little with a %d margin", m_id, m_range.min, m_range.max, margin);
        return false;
    }

    return set_limits(m_range.min + margin, m_range.max - margin);
}

bool ST3215::set_middle_here()
{
    if (!clear_limits())
    {
        return false;
    }

    uint8_t middle = ST3215_TORQUE_MIDDLE;
    if (!unlocked_write(ST3215_REG_TORQUE_ENABLE, &middle, 1) || !set_torque(false))
    {
        return false;
    }

    uint16_t position = 0;
    read_position(position);
    trace("st3215: id %d: middle set here, now at %d; limits cleared, calibrate again", m_id, position);
    return true;
}

bool ST3215::set_id(uint8_t new_id)
{
    if ((new_id >= ST3215_BROADCAST_ID) || (new_id == m_id))
    {
        trace("st3215: id %d: cannot change the id to %d", m_id, new_id);
        return false;
    }

    // The lock is on the servo, which answers to the new id by the time it is locked again.
    if (!m_bus.write_u8(m_id, ST3215_REG_LOCK, 0) || !m_bus.write_u8(m_id, ST3215_REG_ID, new_id))
    {
        return false;
    }

    uint8_t old_id = m_id;
    m_id = new_id;

    if (!m_bus.write_u8(m_id, ST3215_REG_LOCK, 1))
    {
        return false;
    }

    trace("st3215: id %d is now id %d", old_id, new_id);
    return true;
}
