/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/pico_https

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "command_table.h"
#include "shared_buffer.h"
#include "pico_mjson.h"

#ifndef AI_SEQUENCE_BYTES
#define AI_SEQUENCE_BYTES 2048
#endif

#define AI_SEQUENCE_MAX_STEPS 64
#define AI_SEQUENCE_MAX_WAIT_MS 15000

#define AI_SEQUENCE_SETTLE_US 250000ULL
#define AI_SEQUENCE_READY_TIMEOUT_US 4000000ULL

typedef bool (*AiSequenceReady)();

class SequenceCommandContext : public CommandContext
{
public:
    uint32_t send_space() override { return UINT32_MAX; }

protected:
    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;
};

class AiSequence
{
public:
    static AiSequence& get();

    bool load(const char *answer, uint32_t& steps, uint32_t& total_ms, char *why, uint32_t why_size);

    void set_ready(AiSequenceReady ready) { m_ready = ready; }

    void stop();
    void update(uint64_t now_us);

    bool running() const { return m_index < m_count; }

    uint32_t status(char *out, uint32_t size) const;

private:
    AiSequence() = default;

    void run_step();

    SharedBuffer m_commands;
    uint16_t m_at[AI_SEQUENCE_MAX_STEPS] = {};
    uint16_t m_wait_ms[AI_SEQUENCE_MAX_STEPS] = {};
    uint32_t m_count = 0;
    uint32_t m_index = 0;
    uint64_t m_next_us = 0;
    uint64_t m_step_us = 0;
    uint64_t m_now_us = 0;
    AiSequenceReady m_ready = NULL;

    SequenceCommandContext m_command;
};
