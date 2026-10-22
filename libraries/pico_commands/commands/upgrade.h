/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_registration.h"
#include "upgrade_helper.h"

struct Upgrade
{
    static constexpr std::string_view name = "upgrade";
    static constexpr std::string_view help = "upgrade - the .uf2.encrypted firmware, applied once the broker has let go of it and the signature checks out";
    static constexpr std::string_view mqtt_prefix = "";

    static constexpr Payload payload = Payload::Streamed;

	// uf2 is 2-4x the size of the contents, we throw away anything non firmware
    static constexpr uint32_t max_bytes = MAX_BINARY_SIZE*4 + OTA_MAX_HEADER_LEN;
    static constexpr bool clears_retained = true;
    static constexpr bool exclusive = true;

    ~Upgrade();

    bool on_header(CommandContext& ctx);
    bool on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk);
    void on_abort(CommandContext& ctx, const char *reason);
    void on_retained_cleared(CommandContext& ctx);

private:
    UpgradeHelper *m_helper = nullptr;
    bool m_ok = false;
};
