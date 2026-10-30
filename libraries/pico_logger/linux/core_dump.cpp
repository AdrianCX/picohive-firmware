/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "core_dump.h"

bool CoreDump::s_saving = false;

const CoreDump::Header *CoreDump::saved()
{
    return nullptr;
}

uint32_t CoreDump::size()
{
    return 0;
}

const uint8_t *CoreDump::data()
{
    return nullptr;
}

bool CoreDump::clear()
{
    return true;
}
