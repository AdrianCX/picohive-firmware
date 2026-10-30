/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#if PICO_OS_LINUX
// No uart on linux, the bus is simulated (libraries/hardware/linux/mocks/pico_st3215),
typedef struct uart_inst uart_inst_t;
#define uart0 ((uart_inst_t *)0)
#define uart1 ((uart_inst_t *)1)
#else

#include "hardware/uart.h"
#endif
#include "st3215_protocol.h"

// Waveshare ST3215 serial bus servos, any number of them on one data wire.
//
// The data wire is half duplex; it takes two pins of one hardware uart:
//
//   tx_gpio ---[1k]---+--- servo data
//   rx_gpio ----------+
//
// The pico hears everything it sends and throws that echo away. Every call
// waits for the servo's answer, a millisecond or two at 1Mbps.

struct ST3215BusConfig
{
    uart_inst_t *uart = uart0;
    uint8_t tx_gpio = 0;
    uint8_t rx_gpio = 1;
    uint32_t baudrate = 1000000;
    uint32_t timeout_ms = 20;
    uint32_t attempts = 2;
    bool release_tx = true;
};

struct ST3215Line
{
    bool pulled_low = false;
    bool released_high = false;
    bool rx_idle_high = false;
};

class ST3215Bus
{
public:
    bool begin(const ST3215BusConfig& config);
    bool line_test(ST3215Line& line);

    // Moves the whole bus to another baudrate, servos need to be configured separately before this
    bool set_baudrate(uint32_t baudrate);
    uint32_t baudrate() const { return m_config.baudrate; }

    bool ping(uint8_t id);
    bool probe(uint8_t id);

    // see st3215_protocol.h
    bool read(uint8_t id, uint8_t reg, uint8_t *data, uint8_t count);
    bool write(uint8_t id, uint8_t reg, const uint8_t *data, uint8_t count);

    bool read_u8(uint8_t id, uint8_t reg, uint8_t& value);
    bool read_u16(uint8_t id, uint8_t reg, uint16_t& value);
    bool write_u8(uint8_t id, uint8_t reg, uint8_t value);
    bool write_u16(uint8_t id, uint8_t reg, uint16_t value);

    uint8_t last_error() const { return m_last_error; }
    bool saw_noise() const { return m_last_noise; }
    uint32_t echo_misses() const { return m_echo_misses; }
    uint32_t line_errors() const { return m_line_errors; }

private:
    enum class Heard
    {
        Answer,
        Silence,
        Rubbish,
    };

    bool transact(uint8_t id, uint8_t instruction, const uint8_t *params, uint8_t count, uint8_t *reply, uint8_t reply_count);
    bool failed(const char *what, uint8_t id);

    Heard find_status(uint8_t id, const uint8_t *sent, size_t sent_len, uint8_t *status,
                      uint8_t reply_count, uint64_t deadline_us, uint32_t& skipped, bool& echo_intact);

    uint32_t take_line_errors();
    void set_quiet_window();

    void drive_tx();
    void release_tx();

    void note_echo_miss(uint8_t id, uint32_t skipped, uint32_t line_errors);
    uint32_t drain_line(uint8_t id, const char *why, bool settle);

    ST3215BusConfig m_config;
    bool m_started = false;
    bool m_last_noise = false;
    uint8_t m_last_error = 0;
    uint8_t m_id_error[256] = {};

    uint32_t m_quiet_us = 0;
    uint32_t m_echo_misses = 0;
    uint32_t m_line_errors = 0;

    uint64_t m_echo_said_us = 0;
    uint32_t m_echo_misses_said = 0;

    bool m_quiet = false;
    uint32_t m_failures = 0;
    uint32_t m_failures_said = 0;
    uint64_t m_failed_said_us = 0;
};

struct ST3215Settings
{
    uint8_t firmware_major = 0;
    uint8_t firmware_minor = 0;
    uint8_t id = 0;
    uint8_t baud_code = 0;
    uint8_t return_delay = 0;
    uint8_t response_level = 0;
    uint16_t min_angle = 0;
    uint16_t max_angle = 0;
    uint8_t temp_limit = 0;         // C
    uint8_t max_voltage = 0;        // 0.1V
    uint8_t min_voltage = 0;        // 0.1V
    uint16_t max_torque = 0;        // 0..1000
    uint8_t mode = 0;               // 0 is position control
    uint16_t offset = 0;            // where its middle was moved to
    uint8_t voltage = 0;            // as measured right now, 0.1V
    uint8_t temperature = 0;        // as measured right now, C
};

struct ST3215State
{
    uint16_t position = 0;      // 0..4095
    int16_t speed = 0;          // steps/s, signed
    int16_t load = 0;           // -1000..1000 of the maximum torque
    uint8_t voltage = 0;        // 0.1V
    uint8_t temperature = 0;    // C
    int16_t current = 0;        // 6.5mA, signed
    bool moving = false;
};

// One servo on the bus.
//
// Nothing moves until the servo is calibrated: until it has limits narrower than a whole turn
// The limits live in the servo's own EEPROM, so the servo refuses to go past them too, they survive power cycles.
//
class ST3215
{
public:
    ST3215(ST3215Bus& bus, uint8_t id) : m_bus(bus), m_id(id) {}

    bool begin();

    uint8_t id() const { return m_id; }

    void set_default_motion(uint16_t speed, uint8_t acceleration);

    bool move_to(uint16_t position);
    bool move_to(uint16_t position, uint16_t speed, uint8_t acceleration);

    bool move_to_degrees(float degrees);
    bool move_to_degrees(float degrees, uint16_t speed, uint8_t acceleration);

    bool move_by(int16_t steps);

    bool stop();

    bool set_torque(bool on);
    bool torque_on() const { return m_torque_on; }
    bool goal(uint16_t& position) const { position = m_goal; return m_has_goal && m_torque_on; }
    uint8_t default_acceleration() const { return m_acceleration; }

    bool set_torque_limit(uint16_t limit);

    bool read_settings(ST3215Settings& settings);

    uint8_t last_voltage() const { return m_volts; }
    uint8_t last_temperature() const { return m_temperature; }

    bool read_position(uint16_t& position);
    bool read_degrees(float& degrees);
    bool is_moving(bool& moving);
    bool read_state(ST3215State& state);

    bool is_calibrated() const { return (m_min != 0) || (m_max != ST3215_MAX_POSITION); }
    uint16_t min_position() const { return m_min; }
    uint16_t max_position() const { return m_max; }

    bool set_limits(uint16_t min, uint16_t max);
    bool clear_limits();

    bool calibrate_start();
    bool calibrate_update();
    bool calibrate_finish(uint16_t margin = 20);
    void calibrate_cancel() { m_calibrating = false; }
    bool is_calibrating() const { return m_calibrating; }
    const ST3215Range& calibration_range() const { return m_range; }

    bool set_middle_here();
    bool set_id(uint8_t new_id);

private:
    bool unlocked_write(uint8_t reg, const uint8_t *data, uint8_t count);
    bool can_move(const char *what);

    ST3215Bus& m_bus;
    uint8_t m_id;

    uint16_t m_min = 0;
    uint16_t m_max = ST3215_MAX_POSITION;

    uint16_t m_speed = 600;
    uint8_t m_acceleration = 20;

    uint8_t m_volts = 0;
    uint8_t m_temperature = 0;

    bool m_torque_on = false;
    bool m_has_goal = false;
    uint16_t m_goal = 0;
    bool m_calibrating = false;
    ST3215Range m_range;
};
