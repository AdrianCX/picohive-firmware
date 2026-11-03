/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "epaper_handler.h"
#include "pico_logger.h"

EpaperHandler *EpaperHandler::s_instance = NULL;

EpaperHandler& EpaperHandler::getInstance() {
    if (s_instance == NULL)
    {
        s_instance = new EpaperHandler();
    }

    return *s_instance;
}

void EpaperHandler::store_image_data(const u8_t* data, size_t len)
{
    uint32_t bytesToRecv = (m_imageDataIndex + len) > IMAGE_SIZE ? (IMAGE_SIZE - m_imageDataIndex) : len;

    if (bytesToRecv > 0)
    {
        memcpy(&m_imageData[m_imageDataIndex], data, bytesToRecv);
        m_imageDataIndex += bytesToRecv;
    }
}

bool EpaperHandler::show_char(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    return true;
}

bool EpaperHandler::show_char_redblack(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    return true;
}

bool EpaperHandler::show_char_color(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    return true;
}

bool EpaperHandler::show_calendar(const char *data, uint32_t slot, bool force, int xpos, int ypos)
{
    trace("EpaperHandler::show_calendar: mock, slot[%u] force[%d] data[%s]", slot, force, data);
    return true;
}

bool EpaperHandler::show_image()
{
    trace("EpaperHandler::show_image: mock, bytes[%u]", m_imageDataIndex);
    return true;
}

bool EpaperHandler::update(uint64_t now_us)
{
    return false;
}

bool EpaperHandler::busy()
{
    return false;
}

void EpaperHandler::set_sleep_enabled(bool sleep)
{
    m_sleepEnabled = sleep;
}

bool EpaperHandler::save_picture(uint32_t slot)
{
    trace("EpaperHandler::save_picture: mock, slot[%u] bytes[%u]", slot, m_imageDataIndex);
    return m_imageDataIndex == IMAGE_SIZE;
}

bool EpaperHandler::show_picture(uint32_t slot)
{
    trace("EpaperHandler::show_picture: mock, slot[%u]", slot);
    return true;
}

bool EpaperHandler::read_picture(uint32_t slot)
{
    trace("EpaperHandler::read_picture: mock, slot[%u]", slot);
    return true;
}
