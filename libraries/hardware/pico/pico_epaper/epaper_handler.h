/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef EPAPER_HANDLER_H
#define EPAPER_HANDLER_H

#include <stddef.h>
#include <stdint.h>

#include "os_platform.h"

class EpaperHandler {
public:
    static EpaperHandler& getInstance();

    void set_enabled(bool enabled) { m_enabled = enabled; }

    bool show_image();
    void clear_image() { m_imageDataIndex = 0; }
    void store_image_data(const u8_t* data, size_t len);

    bool update(uint64_t now_us);

    bool save_picture(uint32_t slot);
    bool show_picture(uint32_t slot);

#if EPAPER_VARIANT == 1
    bool show_calendar(const char *data, uint32_t slot, bool force = false, int xpos = 280, int ypos=5);
#elif EPAPER_VARIANT == 2
    bool show_calendar(const char *data, uint32_t slot, bool force = false, int xpos = 300, int ypos=5);
#endif

    bool show_char(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint);
    bool show_char_redblack(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint);
    bool show_char_color(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint);

    void set_sleep_enabled(bool sleep);

    bool read_picture(uint32_t slot);

    bool busy();

#if EPAPER_VARIANT == 1
    static const int IMAGE_SIZE = 300 * 448;
    static const int MAX_X = 600;
    static const int MAX_Y = 448;
#elif EPAPER_VARIANT == 2
    static const int IMAGE_SIZE = (648 * 480)/4;
    static const int MAX_X = 648;
    static const int MAX_Y = 480;
#endif
    static const int MAX_SLOT = 10;

protected:
    EpaperHandler() {};

    void saveLastMessage();
    void clear_white();

    static const uint32_t MAX_MESSAGE = 1024;
    char m_lastMessage[MAX_MESSAGE] = {};

    uint8_t m_imageData[IMAGE_SIZE];
    uint32_t m_imageDataIndex = 0;
    uint64_t m_sleepTime = 0;
    bool m_epaperInit = false;
    bool m_messagePending = false;
    bool m_enabled = true;
    bool m_sleepEnabled = false;
    bool m_waitingSleep = false;
    static EpaperHandler *s_instance;
};

#endif
