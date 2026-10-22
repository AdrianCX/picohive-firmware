/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "os_platform.h"
#include "ota_container.h"

#ifndef MAX_BINARY_SIZE
#define MAX_BINARY_SIZE (64 * 1024 * 1024)
#endif

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

    static bool isUpgrading() { return m_isUpgrading; }

private:
    bool onPlaintext(uint8_t *data, size_t len) override;
    void closeStaging(bool remove);

    int m_fd = -1;

    uint8_t m_header[20] = {};
    uint32_t m_bytesWritten = 0;
    uint32_t m_printIndex = 0;

    OtaContainer m_container;
    bool m_verified = false;
    bool m_storeFailed = false;
    const char *m_error = nullptr;

    static bool m_isUpgrading;
};
