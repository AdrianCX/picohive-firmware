/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "session.h"
#include "pico_logger.h"
#include "general_config.h"
#include "http_request.h"
#include <cstdlib>
#include <stdarg.h>

HTTPRequest::HTTPRequest(const char *host, uint16_t port, bool tls, const char *command, const char *path)
{
    init(host, port, tls, command, "%s", path);
}

HTTPRequest *HTTPRequest::create(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, ...)
{
    HTTPRequest *request = new HTTPRequest();

    va_list args;
    va_start(args, pathFormat);
    request->vinit(host, port, tls, command, pathFormat, args);
    va_end(args);

    return request;
}

void HTTPRequest::init(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, ...)
{
    va_list args;
    va_start(args, pathFormat);
    vinit(host, port, tls, command, pathFormat, args);
    va_end(args);
}

void HTTPRequest::vinit(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, va_list args)
{
    trace("HTTPRequest::HTTPRequest: this=%p, host=%s, port=%d, tls=%d, command=%s", this, safestr(host), port, tls, safestr(command));
    
    m_connection.set_callback(this);
    m_connection.set_tls(tls);

    if ((host == NULL) || (command == NULL) || (pathFormat == NULL) || (port == 0))
    {
        trace("HTTPRequest::HTTPRequest: this=%p, host=%s, port=%d, tls=%d, command=%s, error: bad parameters", this, safestr(host), port, tls, safestr(command));

        m_state = FAIL;
        return;
    }

    // first part of the buffer is the host
    m_headerIndex = snprintf(m_header, MAX_HEADER_SIZE, "%s", host);
    if ((m_headerIndex >= MAX_HEADER_SIZE-2) || (m_headerIndex < 0))
    {
        trace("HTTPRequest::HTTPRequest: this=%p, host=%s, port=%d, tls=%d, command=%s, error: host is larger then buffer", this, host, port, tls, command);
        m_state = FAIL;
        return;
    }

    ++m_headerIndex;

    m_headerStart = m_headerIndex;
    m_port = port;

    int index = m_headerIndex;
    int written = snprintf(&m_header[index], MAX_HEADER_SIZE-index, "%s ", command);

    if ((written >= 0) && (index + written < MAX_HEADER_SIZE-2))
    {
        index += written;
        written = vsnprintf(&m_header[index], MAX_HEADER_SIZE-index, pathFormat, args);
    }

    if ((written >= 0) && (index + written < MAX_HEADER_SIZE-2))
    {
        index += written;
        written = snprintf(&m_header[index], MAX_HEADER_SIZE-index, " HTTP/1.0\r\nHost: %s:%d\r\n", host, port);
    }

    if ((written < 0) || (index + written >= MAX_HEADER_SIZE-2))
    {
        trace("HTTPRequest::HTTPRequest: this=%p, host=%s, port=%d, tls=%d, command=%s, failure writing to buffer: %d", this, host, port, tls, command, written);

        m_headerIndex = MAX_HEADER_SIZE;
        m_state = FAIL;
        return;
    }

    m_headerIndex = index + written;
}

HTTPRequest::~HTTPRequest()
{
    trace("HTTPRequest::~HTTPRequest: this=%p", this);
    
    if (m_body != NULL)
    {
        free(m_body);
        m_body = NULL;
    }

    if (m_callback != NULL)
    {
        m_callback->onRequestDestroyed();
        m_callback = NULL;
    }
}

bool HTTPRequest::addHeader(const char *header, const char* value)
{
    if (m_state == FAIL)
    {
        trace("HTTPRequest::addHeader: this=%p, request already failed.", this);
        return false;
    }

    if (m_headerIndex >= MAX_HEADER_SIZE)
    {
        trace("HTTPRequest::addHeader: this=%p, not enough room, increase MAX_HEADER_SIZE", this);
        return false;
    }

    int written = snprintf(&m_header[m_headerIndex], MAX_HEADER_SIZE-m_headerIndex, "%s: %s\r\n", header, value);
    if ((m_headerIndex + written >= MAX_HEADER_SIZE) || (written < 0))
    {
        trace("HTTPRequest::addHeader: this=%p, header=%s, value=%s failure writing to buffer: %d", this, header, value, written);
        m_headerIndex = MAX_HEADER_SIZE;

        return false;
    }

    m_headerIndex += written;
    return true;
}

bool HTTPRequest::setBody(u8_t *data, size_t len)
{
    if (m_state == FAIL)
    {
        trace("HTTPRequest::addHeader: this=%p, request already failed.", this);
        return false;
    }

    if (m_headerIndex >= MAX_HEADER_SIZE)
    {
        trace("HTTPRequest::setBody: this=%p, not enough room, increase MAX_HEADER_SIZE", this);
        return false;
    }

    if (m_body != NULL)
    {
        trace("HTTPRequest::setBody: this=%p, body was already set.", this);
        return false;
    }

    m_bodyLen = len;
    m_body = (u8_t *)malloc(len);
    memcpy(m_body, data, len);
    
    int written = snprintf(&m_header[m_headerIndex], MAX_HEADER_SIZE-m_headerIndex, "Content-Length: %d\r\n", len);

    if ((m_headerIndex + written >= MAX_HEADER_SIZE) || (written < 0))
    {
        trace("HTTPRequest::setBody: this=%p, failure writing content-length to buffer: %d", this, written);
        m_headerIndex = MAX_HEADER_SIZE;
        return false;
    }

    m_headerIndex += written;
    return true;
}

void HTTPRequest::setDebug(bool debug)
{
    m_connection.set_debug(debug);
    m_debug = debug;
}

bool HTTPRequest::send()
{
    if (m_debug)
    {
        trace("HTTPRequest::send: this=%p, m_host=%s, m_port=%d", this, m_header, m_port);
    }

    if (m_state == FAIL)
    {
        trace("HTTPRequest::send: this=%p, request already failed.", this);
        delete this;
        return false;
    }

    if (m_port == 0)
    {
        trace("HTTPRequest::send: this=%p, bad parameters.", this);
        delete this;
        return false;
    }


    if (m_headerIndex >= MAX_HEADER_SIZE-3)
    {
        trace("HTTPRequest::send: this=%p, not enough room for header, increase MAX_HEADER_SIZE", this);
        delete this;
        return false;
    }

    m_header[m_headerIndex++]='\r';
    m_header[m_headerIndex++]='\n';
    m_header[m_headerIndex++]=0;

    m_sending = true;
    err_t err = m_connection.connect(m_header, m_port);
    m_sending = false;

    if (m_closed || ((err != ERR_OK) && (err != ERR_INPROGRESS)))
    {
        trace("HTTPRequest::send: this=%p, m_host=%s, m_port=%d, error=%d, closed=%d", this, m_header, m_port, err, m_closed);
        delete this;
        return false;
    }

    return true;
}

void HTTPRequest::on_connected()
{
    if (m_debug)
    {
        trace("HTTPRequest::on_connected: this=%p, host=%s, header=%s, body=[%.*s] bodyLen=%d", this, m_header, &m_header[m_headerStart], m_bodyLen, m_body, m_bodyLen);
    }
    
    m_connection.send((const u8_t*)&m_header[m_headerStart], m_headerIndex-m_headerStart-1);

    if (m_body != NULL && m_bodyLen > 0)
    {
        m_connection.send(m_body, m_bodyLen);
    }

    m_headerIndex = 0;
}

bool HTTPRequest::on_recv(u8_t *data, size_t len)
{
    if (m_debug)
    {
        trace("HTTPRequest::on_recv: m_state=%d len=%d m_headerIndex=%d", m_state, len, m_headerIndex);
    }
    
    switch (m_state)
    {
        case INIT:
        {
            int processedLen = m_response.parse((char *)data, len);

            if ((processedLen < 0) || (m_response.isFailed()))
            {
                trace("HTTPRequest::on_recv: header failed to parse.");
                return false;
            }

            if (!m_response.isFinished())
            {
                return true;
            }

            m_state = HEADER_RECEIVED;
            if (m_callback != NULL && !m_callback->onHeaderReceived(m_response))
            {
                return false;
            }

            data += processedLen;
            len -= processedLen;
            
            if ((len > 0) && (m_callback != NULL) && (!m_callback->onHttpData(data,len)))
            {
                return false;
            }

            return true;
        }
        case HEADER_RECEIVED:
        {
            return ((m_callback == NULL) || (m_callback->onHttpData(data, len)));
        }
        case WEBSOCKET_ESTABLISHED:
        {
            // future functionality
            return true;
        }
        case FAIL:
        {
            return false;
        }
    }
    return true;
}

void HTTPRequest::on_closed()
{
    trace("HTTPRequest::on_closed: this=%p", this);

    if (m_sending)
    {
        m_closed = true;
        return;
    }

    delete this;
}
