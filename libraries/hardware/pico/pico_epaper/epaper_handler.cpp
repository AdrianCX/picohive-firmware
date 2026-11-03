/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2025 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "epaper_handler.h"
#include "pico_logger.h"
#include "general_config.h"
#include "DEV_Config.h"
#include "fonts.h"

#include "os_fs.h"
#include "screen_state_machine.h"

EpaperHandler *EpaperHandler::s_instance = NULL;

EpaperHandler& EpaperHandler::getInstance() {
    if (s_instance == NULL)
    {
        s_instance = new (std::align_val_t(32)) EpaperHandler();
    }

    return *s_instance;
}

void EpaperHandler::store_image_data(const u8_t* data, size_t len)
{
    uint32_t bytesToRecv = (m_imageDataIndex + len) > IMAGE_SIZE ? (IMAGE_SIZE - m_imageDataIndex) : len;

    trace("EpaperHandler::store_image: this=%p, len[%u] bytesToRecv[%u], m_imageDataIndex[%u]", this, (unsigned int)len, (unsigned int)bytesToRecv, (unsigned int)m_imageDataIndex);

    if (bytesToRecv > 0)
    {
        memcpy(&m_imageData[m_imageDataIndex], data, bytesToRecv);
        m_imageDataIndex += bytesToRecv;
    }
}

void EpaperHandler::clear_white()
{
#if EPAPER_VARIANT == 1
    memset(m_imageData, 0x11, IMAGE_SIZE);
#elif EPAPER_VARIANT == 2
    memset(m_imageData, 0xFF, IMAGE_SIZE / 2);
    memset(m_imageData + IMAGE_SIZE / 2, 0x00, IMAGE_SIZE / 2);
#endif
}

bool EpaperHandler::show_char(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    if ((character < ' ') || (character > '~'))
    {
        character = '.';
    }

#if EPAPER_VARIANT == 1
    return show_char_color(color,character,Xpoint,Ypoint);
#elif EPAPER_VARIANT == 2
    return show_char_redblack(color,character,Xpoint,Ypoint);
#endif
}

bool EpaperHandler::show_char_redblack(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    sFONT* Font = &Font16;

    uint32_t Char_Offset = (character - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (uint32_t Page = 0; Page < Font->Height; Page ++ ) {
        for (uint32_t Column = 0; Column < Font->Width; Column ++ ) {
            if (*ptr & (0x80 >> (Column % 8))) {
                uint32_t address = (Ypoint + Page)*MAX_X + (Xpoint + Column);

                if (((Xpoint + Column) < MAX_X) && ((address/8) < IMAGE_SIZE/2))
                {
                    int byteaddr = address/8;
                    int offset = 7 - (address % 8);
                    if (color == 0x4)
                    {
                        m_imageData[byteaddr] = m_imageData[byteaddr] | (1 << offset);

                        m_imageData[IMAGE_SIZE/2+byteaddr] = m_imageData[IMAGE_SIZE/2+byteaddr] | (1 << offset);
                    }
                    else
                    {
                        m_imageData[byteaddr] = m_imageData[byteaddr] & (~(1 << offset));

                        m_imageData[IMAGE_SIZE/2+byteaddr] = m_imageData[IMAGE_SIZE/2+byteaddr] & (~(1 << offset));
                    }
                }
            }
            if (Column % 8 == 7) {
                ptr++;
            }
        }
        if (Font->Width % 8 != 0) {
            ptr++;
        }
    }

    return true;
}

bool EpaperHandler::show_char_color(uint8_t color, char character, uint32_t Xpoint, uint32_t Ypoint)
{
    sFONT* Font = &Font16;

    uint32_t Char_Offset = (character - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (uint32_t Page = 0; Page < Font->Height; Page ++ ) {
        for (uint32_t Column = 0; Column < Font->Width; Column ++ ) {
            if (*ptr & (0x80 >> (Column % 8))) {
                uint32_t address = (Ypoint + Page)*MAX_X + (Xpoint + Column);

                if (((Xpoint + Column) < MAX_X) && (address/2 < IMAGE_SIZE))
                {
                    if (address % 2 == 0)
                    {
                        m_imageData[address/2] = (m_imageData[address/2] & 0x0f) + (color << 4);
                    }
                    else
                    {
                        m_imageData[address/2] = (m_imageData[address/2] & 0xf0) + (color);
                    }
                }
            }
            if (Column % 8 == 7) {
                ptr++;
            }
        }
        if (Font->Width % 8 != 0) {
            ptr++;
        }
    }

    return true;
}

bool EpaperHandler::show_calendar(const char *data, uint32_t slot, bool force, int xpos, int ypos)
{
    if (busy())
    {
        trace("EpaperHandler::show_calendar: this=%p, a refresh is in progress, skipped", this);
        return false;
    }

    if (!force)
    {
        os_file_t file = {};
        int err = os_fs_open(&file, "lastmessage", OS_FS_READ);
        if (err >= 0)
        {
            m_lastMessage[0] = 0;
            os_fs_read(&file, &m_lastMessage, sizeof(m_lastMessage));
            m_lastMessage[sizeof(m_lastMessage) - 1] = 0;
            os_fs_close(&file);

            size_t diffAt = 0;
            while ((data[diffAt] != 0) && (data[diffAt] == m_lastMessage[diffAt]))
            {
                ++diffAt;
            }

            trace("EpaperHandler::show_calendar: this=%p, new len=%u, last len=%u, first difference at %d", this,
                  (unsigned int)strlen(data), (unsigned int)strlen(m_lastMessage),
                  (data[diffAt] == m_lastMessage[diffAt]) ? -1 : (int)diffAt);

            if (strcmp(data, m_lastMessage) == 0)
            {
                trace("EpaperHandler::show_calendar: this=%p, same message was shown last time", this);

                if (m_sleepEnabled)
                {
                    trace("EpaperHandler::show_calendar: this=%p, going to sleep.", this);
                    m_waitingSleep = true;
                    m_sleepTime = to_us_since_boot(get_absolute_time());
                }
                return true;
            }
        }
        else
        {
            trace("EpaperHandler::show_calendar: this=%p, err=%d failed file open", this, err);
        }
    }
    else
    {
        trace("EpaperHandler::show_calendar: this=%p, forced, removing lastmessage, remove=%d", this, os_fs_remove("lastmessage"));
    }

    const uint32_t maxSlot = MAX_SLOT + 1;
    slot = slot % maxSlot;

    trace("EpaperHandler::show_calendar: this=%p, slot=%u, maxSlot=%u", this, (unsigned int)slot, (unsigned int)maxSlot);

    bool found = false;

    if (xpos != 0 || ypos != 0)
    {
        for (uint32_t i=0;i<maxSlot && !found;++i)
        {
            found = read_picture((slot + i) % maxSlot);
        }
    }

    if (!found)
    {
        trace("EpaperHandler::show_calendar: this=%p, slot=%u, maxSlot=%u - no picture found", this, (unsigned int)slot, (unsigned int)maxSlot);
        clear_white();
    }

    int width = Font16.Width;
    int height = Font16.Height;

    int countx = 0;
    int county = 0;

    uint8_t color = 0x4;

    uint32_t i = 0;
    for (;data[i]!=0 && (ypos + county * height) < MAX_Y;++i)
    {
        if (data[i] > 0 && data[i] < 10)
        {
            color = data[i]-1;
            continue;
        }

        if (data[i] != '\n')
        {
            uint32_t targetx = xpos + countx * width;
            if (targetx + width > MAX_X)
            {
                countx = 0;
                ++county;

                targetx = xpos + countx * width;
            }

            show_char(color, data[i], targetx, ypos + county * height);
            ++countx;
        }
        else
        {
            countx = 0;
            ++county;

            color = 0x0;
        }
    }

    m_imageDataIndex = IMAGE_SIZE;

    bool shown = show_image();

    trace("EpaperHandler::show_calendar: this=%p, shown=%d force=%d len=%u", this, (int)shown, (int)force, (unsigned int)strlen(data));

    if (shown && !force)
    {
        strncpy(&m_lastMessage[0], data, MAX_MESSAGE-1);
        m_lastMessage[MAX_MESSAGE-1] = 0;
        m_messagePending = true;
    }

    return shown;
}

bool EpaperHandler::show_image()
{
    trace("EpaperHandler::show_image: this=%p, m_imageDataIndex=%u start", this, (unsigned int)m_imageDataIndex);

    if (!m_enabled)
    {
        return true;
    }

    if (m_imageDataIndex != IMAGE_SIZE)
    {
        trace("EpaperHandler::show_image: this=%p, m_imageDataIndex=%u, IMAGE_SIZE=%d, incomplete image data, failing display", this, (unsigned int)m_imageDataIndex, IMAGE_SIZE);
        if (m_sleepEnabled)
        {
            trace("EpaperHandler::show_image: this=%p, going to sleep.", this);

            m_waitingSleep = true;
            m_sleepTime = to_us_since_boot(get_absolute_time());
        }
        return false;
    }

    if (!m_epaperInit)
    {
        int err = DEV_Module_Init();
        if(err!=0){
            trace("EpaperHandler::show_image: this=%p err=%d failed initialization", this, err);
            return false;
        }

        m_epaperInit = true;
    }

    trace("EpaperHandler::show_image: this=%p new", this);

    if (!ScreenStateMachine::get()->show_image(&m_imageData[0], IMAGE_SIZE))
    {
        trace("EpaperHandler::show_image: this=%p, a refresh is in progress, not shown", this);
        return false;
    }

    m_imageDataIndex = 0;
    return true;
}

bool EpaperHandler::update(uint64_t now_us)
{
    if (m_waitingSleep)
    {
        if (now_us - m_sleepTime > 4000000)
        {
            general_sleep();
        }
    }

    ScreenStateMachine::get()->update(now_us);

    if (m_messagePending && !ScreenStateMachine::get()->busy())
    {
        m_messagePending = false;

        if (ScreenStateMachine::get()->failed())
        {
            trace("EpaperHandler::update: this=%p, the refresh failed, lastmessage not saved", this);
        }
        else
        {
            saveLastMessage();
        }
    }

    return true;
}

bool EpaperHandler::busy()
{
    return m_messagePending || ScreenStateMachine::get()->busy();
}

void EpaperHandler::saveLastMessage()
{
    os_file_t file = {};
    int err = os_fs_open(&file, "lastmessage", OS_FS_WRITE | OS_FS_CREATE | OS_FS_TRUNCATE);
    if (err != 0)
    {
        trace("EpaperHandler::saveLastMessage: this=%p, err=%d failed file open", this, err);
        return;
    }

    int written = (int)os_fs_write(&file, &m_lastMessage[0], strlen(m_lastMessage) + 1);
    os_fs_close(&file);

    trace("EpaperHandler::saveLastMessage: this=%p, len=%u written=%d", this, (unsigned int)strlen(m_lastMessage), written);
}

void EpaperHandler::set_sleep_enabled(bool sleep)
{
    ScreenStateMachine::get()->set_sleep(sleep);

    m_sleepEnabled = sleep;
}

bool EpaperHandler::save_picture(uint32_t slot)
{
    if (slot > MAX_SLOT)
    {
        trace("EpaperHandler::save_picture: this=%p slot=%u - slots are 0-%d", this, (unsigned int)slot, MAX_SLOT);
        return false;
    }

    if (m_imageDataIndex != IMAGE_SIZE)
    {
        trace("EpaperHandler::save_picture: this=%p slot=%u - incomplete image, %u of %d bytes", this, (unsigned int)slot, (unsigned int)m_imageDataIndex, IMAGE_SIZE);
        return false;
    }

    os_file_t file = {};

    char filename[64];
    snprintf(filename, sizeof(filename), "picture%u", (unsigned int)slot);

    int err = os_fs_open(&file, filename, OS_FS_WRITE | OS_FS_CREATE | OS_FS_TRUNCATE);

    if (err < 0)
    {
        trace("EpaperHandler::save_picture: this=%p err=%d failed storing file", this, err);
        return false;
    }

    int result = os_fs_write(&file, &m_imageData[0], IMAGE_SIZE);

    os_fs_close(&file);

    trace("EpaperHandler::save_picture: this=%p slot=%u result=%d", this, (unsigned int)slot, result);

    if (result != IMAGE_SIZE)
    {
        os_fs_remove(filename);
        return false;
    }

    return true;
}

bool EpaperHandler::show_picture(uint32_t slot)
{
    if (!read_picture(slot))
    {
        return false;
    }

    m_imageDataIndex = IMAGE_SIZE;
    return show_image();
}

bool EpaperHandler::read_picture(uint32_t slot)
{
    if (slot > MAX_SLOT)
    {
        trace("EpaperHandler::read_picture: this=%p slot=%u - slots are 0-%d", this, (unsigned int)slot, MAX_SLOT);
        return false;
    }

    os_file_t file = {};

    char filename[64];
    snprintf(filename, sizeof(filename), "picture%u", (unsigned int)slot);

    int err = os_fs_open(&file, filename, OS_FS_READ);

    if (err < 0)
    {
        trace("EpaperHandler::read_picture: this=%p slot=%u err=%d failed reading file", this, (unsigned int)slot, err);
        return false;
    }

    int result = os_fs_read(&file, &m_imageData[0], IMAGE_SIZE);
    os_fs_close(&file);

    trace("EpaperHandler::read_picture: this=%p slot=%u result=%d", this, (unsigned int)slot, result);
    return result == IMAGE_SIZE;
}
