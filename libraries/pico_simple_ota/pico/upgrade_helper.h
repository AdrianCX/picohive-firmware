/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "general_config.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "http_session.h"
#include "hardware/flash.h"

#include "ota_container.h"

#include "flash_layout.h"

#ifndef MAX_BINARY_SIZE
#define MAX_BINARY_SIZE PICO_OS_SLOT_SIZE
#endif

static_assert(MAX_BINARY_SIZE <= PICO_OS_SLOT_SIZE, "MAX_BINARY_SIZE is larger than the firmware slot in flash_layout.h");

class UpgradeHelper : public IOtaPlaintextSink
{
    UpgradeHelper();
public:
    static UpgradeHelper *create();
    virtual ~UpgradeHelper();
    
    bool clear();
    bool storeData(const u8_t *data, size_t len);
    bool verifySignature();
    bool upgrade();

    const char *buildId() const { return m_container.buildId(); }

    const char *lastError() const;

    uint32_t bytesWritten() const { return m_bytesWritten; }

    static const int NUM_REGIONS = MAX_BINARY_SIZE / FLASH_SECTOR_SIZE;

    uint8_t *getRegions() { return &m_regionErased[0]; }

    static bool isUpgrading() { return m_isUpgrading; }

    
private:
    bool onPlaintext(uint8_t *data, size_t len) override;

    static void writeToFlash(void *self);
    inline void setRegionErased(uint32_t region) { m_regionErased[region/8] |= (1 << (region % 8)); }
    inline bool getRegionErased(uint32_t region) { return (m_regionErased[region/8] & (1 << (region % 8))) != 0; }

    static const int BLOCK_SIZE = 512;
    alignas(32) uint8_t m_buffer[BLOCK_SIZE] = {};

    uint8_t m_regionErased[(NUM_REGIONS + 7) / 8] = {};
    uint32_t m_index = 0;

    uint32_t m_printIndex = 0;
    uint32_t m_bytesWritten = 0;

    OtaContainer m_container;
    bool m_verified = false;
    bool m_storeFailed = false;
    const char *m_error = nullptr;

    static bool m_isUpgrading;
};
