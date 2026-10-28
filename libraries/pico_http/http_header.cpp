/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "string.h"

#include "os_platform.h"

#include "http_header.h"

#define FALLTHROUGH [[fallthrough]]

extern "C" void trace(const char *parameters, ...);
extern "C" const char *safestr(const char *value);

HTTPHeader::HTTPHeader()
{
}

void HTTPHeader::release()
{
    reset();
    m_buffer = SharedBuffer();
    m_text = nullptr;
}

void HTTPHeader::reset()
{
    m_isRequest = false;
    m_currentHeader = 0;
    m_responseCode = 0;
    m_start = 0;
    m_index = 0;
    m_path = NULL;
    m_state = STATE_FIRST;

    for (int i=0;i<MAX_HEADERS;++i)
    {
        m_headerValue[i] = NULL;
    }
}

void HTTPHeader::print()
{
    if (isRequest())
    {
        trace("HTTPHeader::print: this=%p, command[%s] path[%s] numHeaders[%d]\n", this, safestr(m_text), safestr(m_path), m_numHeaders);
    }
    else
    {
        trace("HTTPHeader::print: this=%p, response[%s] numHeaders[%d]\n", this, safestr(m_path), m_numHeaders);
    }
    for (int i=0;i<m_numHeaders;++i)
    {
        trace("HTTPHeader::print: this=%p, name[%s] value[%s]\n", this, safestr(m_headerName[i]), safestr(m_headerValue[i]));
    }
}

const char *HTTPHeader::getHeaderValue(const char *headerName)
{
    for (int i=0;i<m_numHeaders;++i)
    {
        if (strcasecmp(m_headerName[i], headerName) == 0)
        {
            //trace("HTTPHeader::getHeaderValue: this=%p, found header[%s] value[%s]\n", this, safestr(headerName), safestr(m_headerValue[i]));
            return m_headerValue[i];
        }
    }
    trace("HTTPHeader::getHeaderValue: this=%p, could not find[%s]\n", this, safestr(headerName));
    return NULL;
}

bool HTTPHeader::addWantedHeader(const char *header)
{
    if ((m_numHeaders == MAX_HEADERS) || (header == NULL))
    {
        return false;
    }
    m_headerName[m_numHeaders++] = header;

    return true;
}

// only care about these headers typically:
//date
//authorization
//content-length
//sec-websocket-key

bool HTTPHeader::parse_chunk(char *data, int& len, int& i)
{
    switch (m_state)
    {
        case STATE_FIRST:
        {
            for (;i<len && data[i]!=' ' && data[i] != '\t';++i)
            {
                if (m_index == MAX_HEADER_SIZE) { return false; }
                m_text[m_index++] = data[i];
            }
            
            if (i==len)
            {
                return true;
            }
            
            if (m_index == MAX_HEADER_SIZE) { return false; }
            m_text[m_index++] = 0;
            m_state = STATE_CMDTYPE;
            m_path = &m_text[m_index];

            ++i;

            FALLTHROUGH;
        }
        case STATE_CMDTYPE:
        {
            if (i == len)
            {
                return true;
            }

            m_isRequest = (data[i] == '/');
            m_state = STATE_SECOND;

            FALLTHROUGH;
        }
        case STATE_SECOND:
        {
            for (;i<len && data[i]!=' ' && data[i] != '\t';++i)
            {
                if (isResponse())
                {
                    if ((data[i] < '0') || (data[i] > '9'))
                    {
                        return false;
                    }
                    
                    m_responseCode = m_responseCode * 10 + (data[i] - '0');

                    if (m_responseCode > 1000)
                    {
                        return false;
                    }
                }
                else
                {
                    if (m_index == MAX_HEADER_SIZE) { return false; }
                    m_text[m_index++] = data[i];
                }
            }
            
            if (i == len)
            {
                return true;
            }

            if (m_index == MAX_HEADER_SIZE) { return false; }
            m_text[m_index++] = 0;
            m_state = STATE_SKIP;

            ++i;

            FALLTHROUGH;
        }
        case STATE_SKIP:
        {
            for (;i<len && data[i]!='\n';++i) {}
            
            if (i == len)
            {
                return true;
            }

            m_state = STATE_NAME;
            m_start = m_index;

            ++i;
            
            FALLTHROUGH;
        }
        case STATE_NAME:
        {
            for (;i<len && data[i]!='\n' && data[i] != ':';++i)
            {
                if (m_index == MAX_HEADER_SIZE) { return false; }
                m_text[m_index++] = data[i];
            }
            
            if (i==len)
            {
                return true;
            }

            if (data[i] == '\n')
            {
                ++i;
                
                if ((m_index > m_start) && (m_text[m_start] == '\r'))
                {
                    m_state = STATE_FINISHED;
                    return true;
                }

                return false;
            }

            if (m_index == MAX_HEADER_SIZE) { return false; }
            m_text[m_index++] = 0;

            ++i;

            int header=0;
            for (; header<m_numHeaders && (strcasecmp(m_headerName[header], &m_text[m_start]) != 0); ++header)
            {
            }

            if (header == m_numHeaders)
            {
                // unwanted header, skip
                m_index = m_start;
                m_state = STATE_SKIP;
                return true;
            }

            m_state = STATE_SKIP_SPACES;
            m_start = m_index;
            m_currentHeader = header;

            FALLTHROUGH;
        }
        case STATE_SKIP_SPACES:
        {
            for (;i<len && data[i]!='\r' && (data[i] == ' ' || data[i] == '\t');++i) {}
            
            if (i==len) { return true; }
            m_state = STATE_VALUE;
            
            FALLTHROUGH;
        }
        case STATE_VALUE:
        {
            for (;i<len && data[i]!='\n'; ++i)
            {
                if (m_index == MAX_HEADER_SIZE) { return false; }
                m_text[m_index++] = data[i];
            }
            
            if (i==len) { return true; }

            if (m_index == MAX_HEADER_SIZE) { return false; }

            // The \r needs to go away
            if ((m_index > m_start) && (m_text[m_index-1] == '\r'))
            {
                m_text[m_index-1] = 0;
            }
            else
            {
                m_text[m_index++] = 0;
            }

            m_headerValue[m_currentHeader] = &m_text[m_start];
            m_state = STATE_NAME;
            m_start = m_index;

            ++i;
            break;
        }
        case STATE_FAILED:
        case STATE_FINISHED:
        default:
        {
            // should never reach this, caller should check before calling
             return false;
        }
    }

    return true;
}

int32_t HTTPHeader::parse(char *data, int len)
{
    // Taken when the first bytes come, given back by release().
    if (m_text == nullptr)
    {
        m_buffer = SharedBuffer(MAX_HEADER_SIZE - 1);
        m_text = m_buffer.data();

        if (m_text == nullptr)
        {
            m_state = STATE_FAILED;
            return -1;
        }
    }

    int i=0;
    while ((i < len) && (m_state != STATE_FINISHED))
    {
        if (!parse_chunk(data, len, i))
        {
            m_state = STATE_FAILED;
            return -1;
        }
    }

    return i;
}

