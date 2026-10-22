/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "common_functions.h"
#include "config_store.h"
#include "config_write.h"

bool ConfigWrite::on_payload(CommandContext& ctx, std::string_view body)
{
    JsonView json(body.data(), body.size());

    if (!json.is_valid() || (json.type() != JsonType::Object))
    {
        return ctx.reply("not a JSON object");
    }

    if (!ConfigStore::update(body.data(), body.size()))
    {
        return ctx.reply("saving failed");
    }

    StoredConfig stored;

    apply_config(stored.json());

    if (app_info().on_config_update != nullptr)
    {
        app_info().on_config_update(stored.json());
    }

    return true;
}

WS_COMMAND(ConfigWrite);
