/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_dispatcher.h"

class CaptureCommandContext : public CommandContext
{
public:
    uint32_t left = 0;
    uint32_t written = 0;

    uint32_t send_space() override { return UINT32_MAX; }

    bool run(std::span<const Command> table, std::string_view topic, uint32_t limit,
             std::string_view body = std::string_view());

    void run_each(std::span<const Command> table, uint32_t limit);

protected:
    virtual void take(const char *text, uint32_t len) = 0;

    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;
};
