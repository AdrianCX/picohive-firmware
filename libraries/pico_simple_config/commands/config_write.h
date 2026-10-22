/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "app_config.h"
#include "command_registration.h"
#include "config_store.h"

struct ConfigWrite
{
    static constexpr std::string_view name = "config/write";
    static constexpr std::string_view help = "config/write - every setting as JSON, as config/read sends them; empty secrets are kept, null ones cleared";

    static constexpr Payload payload = Payload::Buffered;
    static constexpr uint32_t max_bytes = CONFIG_MAX_JSON;

    bool on_payload(CommandContext& ctx, std::string_view body);
};
