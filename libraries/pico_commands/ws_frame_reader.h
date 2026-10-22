/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware
   
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <stdint.h>

#include "command_dispatcher.h"

#define WS_PATH_LEN 128

// Turns websocket frames into command messages, frame format is: [path, NUL, body]
class WsFrameReader
{
public:
    bool feed(CommandDispatcher& dispatcher, uint8_t *data, uint32_t len, uint64_t offset, uint64_t length);

private:
    char m_path[WS_PATH_LEN] = {};
    uint32_t m_path_len = 0;
    bool m_in_path = false;
    bool m_too_long = false;
    bool m_dispatched = false;
};
