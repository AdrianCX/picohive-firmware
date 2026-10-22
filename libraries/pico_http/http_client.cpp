/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <cstdlib>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico_logger.h"
#include "http_client.h"
#include "http_header_helper.h"

HTTPClient::HTTPClient(const char *host, uint16_t port, bool tls)
    : m_host(host)
    , m_port(port)
{
    trace("HTTPClient::HTTPClient: this=%p host=%s port=%d tls=%d", this, safestr(host), port, tls);

    m_connection.set_callback(this);
    m_connection.set_tls(tls);

    m_response.addWantedHeader("Content-Length");
    m_response.addWantedHeader("Transfer-Encoding");
    m_response.addWantedHeader("Connection");
}

HTTPClient::~HTTPClient()
{
    trace("HTTPClient::~HTTPClient: this=%p host=%s port=%d", this, safestr(m_host), m_port);

    m_callback = NULL;
    m_connection.set_callback(NULL);

    if (m_body_owned)
    {
        free(m_body);
    }

    m_body = NULL;
}

bool HTTPClient::setTarget(const char *host, uint16_t port, bool tls)
{
    if (busy())
    {
        trace("HTTPClient::setTarget: this=%p, a request is in flight", this);
        return false;
    }

    if (m_connection.is_connected())
    {
        m_connection.close();
    }

    m_drop_pending = false;
    m_host = host;
    m_port = port;
    m_connection.set_tls(tls);

    trace("HTTPClient::setTarget: this=%p host=%s port=%d tls=%d", this, safestr(host), port, tls);
    return true;
}

const char *HTTPClient::stateName() const
{
    switch (m_state)
    {
        case State::Idle:           return "idle";
        case State::Connecting:     return "connecting";
        case State::Waiting:        return "waiting for the answer";
        case State::Header:         return "reading the header";
        case State::BodyLength:     return "reading the body";
        case State::BodyChunked:    return "reading the chunked body";
        case State::BodyUntilClose: return "reading the body until close";
        case State::Retry:          return "retrying";
    }

    return "unknown";
}

void HTTPClient::setDebug(bool debug)
{
    m_connection.set_debug(debug);
    m_debug = debug;
}

bool HTTPClient::begin(const char *command, const char *path)
{
    return start_request(command, path) && m_request.append(path) && end_request_line(command);
}

bool HTTPClient::beginf(const char *command, const char *pathFormat, ...)
{
    va_list args;
    va_start(args, pathFormat);
    bool ok = vbeginf(command, pathFormat, args);
    va_end(args);

    return ok;
}

bool HTTPClient::vbeginf(const char *command, const char *pathFormat, va_list args)
{
    return start_request(command, pathFormat) && m_request.append_vsnprintf(pathFormat, args) && end_request_line(command);
}

// "<command> " - the path follows, then end_request_line().
bool HTTPClient::start_request(const char *command, const char *path)
{
    m_ready = false;

    if (busy())
    {
        trace("HTTPClient::begin: this=%p, a request is already in flight", this);
        return false;
    }

    if ((m_host == NULL) || (command == NULL) || (path == NULL) || (m_port == 0))
    {
        trace("HTTPClient::begin: this=%p, bad parameters", this);
        return false;
    }

    m_request = SharedBuffer(MAX_REQUEST_SIZE - 1);

    if (!m_request)
    {
        trace("HTTPClient::begin: this=%p, no memory for the request", this);
        return false;
    }

    return m_request.append(command, " ");
}

bool HTTPClient::end_request_line(const char *command)
{
    if (!m_request.append(" HTTP/1.1\r\nHost: ", m_host) || !m_request.append_snprintf(":%d\r\n", m_port))
    {
        trace("HTTPClient::begin: this=%p, command=%s, request does not fit %d bytes", this, command, MAX_REQUEST_SIZE);
        return false;
    }

    m_head = (strcasecmp(command, "HEAD") == 0);
    m_ready = true;

    trace("HTTPClient::begin: this=%p host=%s command=%s request=%u bytes", this, m_host, command, (unsigned int)m_request.size());
    return true;
}

bool HTTPClient::addHeader(const char *header, const char *value)
{
    if (!m_ready)
    {
        return false;
    }

    if ((header == NULL) || (value == NULL) || !m_request.append(header, ": ", value, "\r\n"))
    {
        trace("HTTPClient::addHeader: this=%p, header=%s does not fit", this, safestr(header));
        m_ready = false;
        return false;
    }

    return true;
}

bool HTTPClient::send(const u8_t *body, size_t len)
{
    u8_t *copy = NULL;

    if ((body != NULL) && (len > 0))
    {
        copy = (u8_t *)malloc(len);

        if (copy == NULL)
        {
            trace("HTTPClient::send: this=%p, no memory for a %u byte body", this, (unsigned int)len);
            return false;
        }

        memcpy(copy, body, len);
    }

    return start_send(copy, len, true);
}

bool HTTPClient::send_borrowed(const u8_t *body, size_t len)
{
    return start_send((u8_t *)body, len, false);
}

bool HTTPClient::send(const SharedBuffer& body)
{
    m_body_buffer = body;

    if (!start_send((u8_t *)body.c_str(), body.size(), false))
    {
        m_body_buffer = SharedBuffer();
        return false;
    }

    return true;
}

bool HTTPClient::send(const BufferChain& body)
{
    if (!m_ready || busy())
    {
        return start_send(NULL, 0, false);
    }

    m_body_chain = body;

    if (!start_send(NULL, body.size(), false))
    {
        m_body_chain = BufferChain();
        return false;
    }

    return true;
}

bool HTTPClient::start_send(u8_t *body, size_t len, bool owned)
{
    if (!m_ready || busy())
    {
        trace("HTTPClient::send: this=%p, no request to send, ready=%d state=%d", this, m_ready, (int)m_state);
        if (owned) free(body);
        return false;
    }

    m_ready = false;

    bool has_body = (body != NULL) || !m_body_chain.empty();
    bool fits = has_body ? m_request.append_snprintf("Content-Length: %u\r\n\r\n", (unsigned int)len) : m_request.append("\r\n");

    if (!fits)
    {
        trace("HTTPClient::send: this=%p, request does not fit %d bytes", this, MAX_REQUEST_SIZE);
        if (owned) free(body);
        return false;
    }

    if (m_body_owned)
    {
        free(m_body);
    }

    m_body = body;
    m_body_len = (body != NULL) ? len : 0;
    m_body_owned = owned;

    m_response.reset();
    m_body_left = 0;
    m_keep_alive = false;
    m_retried = false;

    if (m_connection.is_connected())
    {
        m_reused = true;
        m_state = State::Waiting;
        write_request();
    }
    else
    {
        m_reused = false;
        connect();
    }

    return true;
}

void HTTPClient::update()
{
    if (m_drop_pending)
    {
        m_drop_pending = false;
        m_connection.close();
    }

    if (m_state == State::Retry)
    {
        trace("HTTPClient::update: this=%p, reconnecting to retry the request", this);

        m_retried = true;
        m_reused = false;
        m_response.reset();
        connect();
    }
}

void HTTPClient::close()
{
    trace("HTTPClient::close: this=%p host=%s state=%d", this, m_host, (int)m_state);

    m_drop_pending = false;
    m_connection.close();

    // on_closed() has normally finished it already, unless it was waiting to retry.
    finish(false);
}

void HTTPClient::connect()
{
    trace("HTTPClient::connect: this=%p host=%s", this, m_host);

    m_state = State::Connecting;

    err_t err = m_connection.connect(m_host, m_port);

    // A failure past dns has already gone through on_closed().
    if ((err != ERR_OK) && (err != ERR_INPROGRESS) && (m_state == State::Connecting))
    {
        trace("HTTPClient::connect: this=%p, host=%s, port=%d, err=%d", this, safestr(m_host), m_port, err);
        finish(false);
    }
}

void HTTPClient::write_request()
{
    m_sent = 0;
    m_all_sent = false;
    write_more();
}

uint32_t HTTPClient::piece_at(uint32_t at, const u8_t *&data) const
{
    if (at < m_request.size())
    {
        data = (const u8_t *)m_request.c_str() + at;
        return m_request.size() - at;
    }

    at -= m_request.size();

    if (m_body != NULL)
    {
        data = m_body + at;
        return (at < m_body_len) ? (uint32_t)(m_body_len - at) : 0;
    }

    const char *piece = NULL;
    uint32_t len = m_body_chain.piece(at, piece);
    data = (const u8_t *)piece;
    return len;
}

void HTTPClient::write_more()
{
    if (m_writing)
    {
        return;
    }

    m_writing = true;
    err_t err = ERR_OK;

    while ((err == ERR_OK) && (m_state == State::Waiting) && !m_all_sent)
    {
        const u8_t *data = NULL;
        uint32_t len = piece_at(m_sent, data);

        if (len == 0)
        {
            m_all_sent = true;
            break;
        }

        len = (len < SEND_PIECE) ? len : SEND_PIECE;

        if (m_connection.send_buffer_size() < len)
        {
            break;
        }

        err = m_connection.send(data, len);

        if (err == ERR_OK)
        {
            m_sent += len;
        }
    }

    m_writing = false;

    if (err != ERR_OK)
    {
        trace("HTTPClient::write_request: this=%p, err=%d, %u bytes sent", this, err, (unsigned int)m_sent);
        m_drop_pending = true;
    }
}

bool HTTPClient::on_sent(u16_t len)
{
    write_more();
    return true;
}

void HTTPClient::on_connected()
{
    trace("HTTPClient::on_connected: this=%p host=%s state=%d", this, m_host, (int)m_state);

    if (m_state != State::Connecting)
    {
        return;
    }

    m_state = State::Waiting;
    write_request();
}

bool HTTPClient::on_recv(u8_t *data, size_t len)
{
    switch (m_state)
    {
        case State::Waiting:
        case State::Header:
        {
            // An answer is coming: the request is not sent again.
            m_request = SharedBuffer();
            m_state = State::Header;

            int32_t processed = m_response.parse((char *)data, len);

            if ((processed < 0) || m_response.isFailed())
            {
                trace("HTTPClient::on_recv: this=%p, header failed to parse", this);
                finish(false);
                return false;
            }

            if (!m_response.isFinished())
            {
                return true;
            }

            data += processed;
            len -= processed;

            uint16_t code = m_response.getResponseCode();

            if ((code >= 100) && (code < 200))
            {
                // An interim answer, like 100 Continue: the real one follows.
                m_response.reset();
                return (len == 0) || on_recv(data, len);
            }

            if (!start_body())
            {
                finish(false);
                return false;
            }

            return on_body(data, len);
        }
        case State::BodyLength:
        case State::BodyChunked:
        case State::BodyUntilClose:
        {
            return on_body(data, len);
        }
        default:
        {
            // Nothing was asked: the connection is out of step, start over.
            trace("HTTPClient::on_recv: this=%p, %u unexpected bytes in state %d", this, (unsigned int)len, (int)m_state);
            return false;
        }
    }
}

bool HTTPClient::start_body()
{
    uint16_t code = m_response.getResponseCode();
    const char *version = m_response.getVersion();
    const char *length = m_response.getHeaderValue("Content-Length");
    const char *encoding = m_response.getHeaderValue("Transfer-Encoding");
    const char *connection = m_response.getHeaderValue("Connection");

    // HTTP/1.1 keeps the connection unless told otherwise, 1.0 only when asked to.
    if ((version != NULL) && (strcasecmp(version, "HTTP/1.0") == 0))
    {
        m_keep_alive = HTTPHeaderHelper::hasToken(connection, "keep-alive");
    }
    else
    {
        m_keep_alive = !HTTPHeaderHelper::hasToken(connection, "close");
    }

    HTTPHeaderHelper::TransferCoding coding = HTTPHeaderHelper::transferCoding(encoding);

    if (m_head || (code == 204) || (code == 304))
    {
        m_body_left = 0;
        m_state = State::BodyLength;
    }
    else if (coding == HTTPHeaderHelper::TransferCoding::Unsupported)
    {
        trace("HTTPClient::start_body: this=%p, transfer-encoding %s is not supported", this, encoding);
        return false;
    }
    else if (coding == HTTPHeaderHelper::TransferCoding::Chunked)
    {
        // A Content-Length next to chunked is ignored, but a server sending
        // both is not trusted with the next answer on this connection.
        m_keep_alive = m_keep_alive && (length == NULL);
        m_chunks.reset();
        m_state = State::BodyChunked;
    }
    else if (length != NULL)
    {
        if (!HTTPHeaderHelper::parseContentLength(length, m_body_left))
        {
            trace("HTTPClient::start_body: this=%p, bad content-length %s", this, length);
            return false;
        }

        m_state = State::BodyLength;
    }
    else
    {
        m_keep_alive = false;
        m_state = State::BodyUntilClose;
    }

    return (m_callback == NULL) || m_callback->onHeaderReceived(m_response);
}

bool HTTPClient::on_body(u8_t *data, size_t len)
{
    switch (m_state)
    {
        case State::BodyUntilClose:
        {
            if ((len > 0) && (m_callback != NULL) && !m_callback->onHttpData(data, len))
            {
                finish(false);
                return false;
            }

            return true;
        }
        case State::BodyChunked:
        {
            size_t used = 0;
            HTTPChunkDecoder::Result result = m_chunks.feed(data, len, used, *this);

            if (result == HTTPChunkDecoder::Result::Error)
            {
                trace("HTTPClient::on_body: this=%p, chunked body: %s", this, m_chunks.error());
                finish(false);
                return false;
            }

            if (result == HTTPChunkDecoder::Result::More)
            {
                return true;
            }

            if (used < len)
            {
                trace("HTTPClient::on_body: this=%p, %u bytes past the end of the answer", this, (unsigned int)(len - used));
            }

            return complete(used < len);
        }
        default:
        {
            bool extra = (len > m_body_left);

            if (extra)
            {
                trace("HTTPClient::on_body: this=%p, %u bytes past the end of the answer", this, (unsigned int)(len - m_body_left));
                len = m_body_left;
            }

            if ((len > 0) && (m_callback != NULL) && !m_callback->onHttpData(data, len))
            {
                finish(false);
                return false;
            }

            m_body_left -= len;

            if (m_body_left > 0)
            {
                return true;
            }

            return complete(extra);
        }
    }
}

bool HTTPClient::onChunkData(uint8_t *data, size_t len)
{
    return (m_callback == NULL) || m_callback->onHttpData(data, len);
}

bool HTTPClient::complete(bool extra)
{
    // Bytes nobody asked for mean the connection is out of step, as does an
    // answer before the whole request went out.
    bool keep = m_keep_alive && !extra && m_all_sent;

    finish(true);
    return keep;
}

void HTTPClient::on_closed()
{
    trace("HTTPClient::on_closed: this=%p host=%s state=%d", this, m_host, (int)m_state);

    m_drop_pending = false;

    switch (m_state)
    {
        case State::Idle:
        case State::Retry:
        {
            return;
        }
        case State::Waiting:
        {
            // The server can close a kept connection just as a request goes
            // out on it. Nothing came back, so ask once more on a new one.
            if (m_reused && !m_retried)
            {
                m_state = State::Retry;
                return;
            }
            break;
        }
        case State::BodyUntilClose:
        {
            finish(true);
            return;
        }
        default:
        {
            break;
        }
    }

    trace("HTTPClient::on_closed: this=%p, closed before the answer was complete, state=%d", this, (int)m_state);
    finish(false);
}

void HTTPClient::finish(bool ok)
{
    if (m_state == State::Idle)
    {
        return;
    }

    trace("HTTPClient::finish: this=%p host=%s ok=%d state=%d", this, m_host, ok, (int)m_state);

    m_state = State::Idle;

    if (m_body_owned)
    {
        free(m_body);
    }

    m_body = NULL;
    m_body_len = 0;
    m_body_owned = false;
    m_body_buffer = SharedBuffer();
    m_body_chain = BufferChain();
    m_request = SharedBuffer();

    if (m_callback != NULL)
    {
        m_callback->onResponseComplete(ok);
    }

    if (m_state == State::Idle)
    {
        m_response.release();
    }
}
