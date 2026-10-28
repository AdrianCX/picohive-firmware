/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "mbedtls/base64.h"
// No C++ guard in this header.
extern "C" {
#include "mbedtls/constant_time.h"
}

#include "app_config.h"
#include "command_request_handler.h"
#include "pico_helper.h"
#include "pico_logger.h"

static const uint8_t *HTTP_BODY = nullptr;
static unsigned int HTTP_BODY_LEN = 0;
static bool AUTH_REQUIRED = false;
static const uint32_t FRAME_OVERHEAD = 16;

void CommandRequestHandler::set_config(const uint8_t *http_body, unsigned int body_len)
{
    HTTP_BODY = http_body;
    HTTP_BODY_LEN = body_len;
}

void CommandRequestHandler::require_auth(bool required)
{
    AUTH_REQUIRED = required;
}

void CommandRequestHandler::create(void *arg, bool tls)
{
    new CommandRequestHandler(arg, tls);
}

CommandRequestHandler::WebSocketState::WebSocketState(CommandRequestHandler *handler)
    : dispatcher(WsCommandSet::all(), context, slots)
{
    context.handler = handler;
}

CommandRequestHandler::CommandRequestHandler(void *arg, bool tls)
    : HTTPSession(arg, tls)
    , m_index(HTTP_BODY_LEN)
{
    trace("CommandRequestHandler::CommandRequestHandler: this=%p, arg=%p", this, arg);

    addWantedHeader("Authorization");
    addWantedHeader("Content-Length");
    addWantedHeader("Sec-WebSocket-Key");
    addWantedHeader("Origin");
    addWantedHeader("Host");
}

CommandRequestHandler::~CommandRequestHandler()
{
    if (m_ws != nullptr)
    {
        m_ws->dispatcher.abort("the websocket closed");
        delete m_ws;
    }
}

bool CommandRequestHandler::isAuthorized(HTTPHeader& header)
{
    const Config& config = get_config();
    size_t user_len = strlen(config.web_user);
    size_t pass_len = strlen(config.web_pass);

    if ((user_len == 0) && (pass_len == 0))
    {
        return true;
    }

    const char *auth = header.getHeaderValue("Authorization");
    size_t len = 0;
    char login[2 * MAX_CONFIG_STR + 2];

    if ((auth == nullptr) || (strncasecmp(auth, "Basic ", 6) != 0))
    {
        return false;
    }

    if (mbedtls_base64_decode((unsigned char *)login, sizeof(login), &len, (const unsigned char *)auth + 6, strlen(auth + 6)) != 0)
    {
        trace("CommandRequestHandler::isAuthorized: this=%p, bad base64 or too long Authorization", this);
        return false;
    }

    bool ok = (len == user_len + 1 + pass_len) &&
        (memcmp(login, config.web_user, user_len) == 0) &&
        (login[user_len] == ':') &&
        (mbedtls_ct_memcmp(login + user_len + 1, config.web_pass, pass_len) == 0);

    memset(login, 0, len);

    if (!ok)
    {
        trace("CommandRequestHandler::isAuthorized: this=%p, wrong login", this);
    }

    return ok;
}

bool CommandRequestHandler::onRequestReceived(HTTPHeader& header)
{
    if (AUTH_REQUIRED)
    {
        if (!isAuthorized(header))
        {
            const char *unauthorized = "HTTP/1.1 401 Unauthorized\r\n"
                "WWW-Authenticate: Basic realm=\"PicoServer\"\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n"
                "\r\n";

            return sendHttpReplyRaw(unauthorized, strlen(unauthorized), true);
        }
    }

    if (strcmp(header.getCommand(), "GET") == 0)
    {
        if (strcmp(header.getPath(), "/websocket") == 0)
        {
            const char *origin = header.getHeaderValue("Origin");
            const char *host = header.getHeaderValue("Host");
            const char *origin_host = (origin != nullptr) ? strstr(origin, "://") : nullptr;

            if (PicoHelper::is_domain(host) || ((origin != nullptr) && ((origin_host == nullptr) || (strcasecmp(origin_host + 3, host) != 0))))
            {
                trace("CommandRequestHandler::onRequestReceived: this=%p, websocket refused, origin[%s] host[%s]", this, origin, safestr(host));

                const char *forbidden = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                return sendHttpReplyRaw(forbidden, strlen(forbidden), true);
            }

            trace("CommandRequestHandler::onRequestReceived: this=%p, acceptWebSocket.", this);
            m_ws = new WebSocketState(this);
            return acceptWebSocket(header);
        }

        trace("CommandRequestHandler::onRequestReceived: this=%p, sendReply[index.html]:", this);

        char reply[192];
        int n = snprintf(reply, sizeof(reply), "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Encoding: gzip\r\nConnection: close\r\nContent-Length: %d\r\n\r\n", HTTP_BODY_LEN);

        if ((n < 0) || (n >= (int)sizeof(reply)))
        {
            trace("CommandRequestHandler::onRequestReceived: this=%p, buffer to small, expected[%d] got[%d]:\n", this, n, (int)sizeof(reply));
            return false;
        }

        // body could be very large so send it buffered
        if (sendHttpReplyRaw(reply, n, true, HTTP_BODY_LEN))
        {
            m_index = 0;
            return sendIndexHtmlBody();
        }
    }

    return false;
}

bool CommandRequestHandler::sendIndexHtmlBody()
{
    if ((m_index < HTTP_BODY_LEN) && (send_buffer_size() > TCP_MSS))
    {
        uint32_t socket_buffer_size = send_buffer_size() - TCP_MSS;
        uint32_t remaining = HTTP_BODY_LEN - m_index;
        uint32_t len = (socket_buffer_size < remaining) ? socket_buffer_size : remaining;

        uint32_t index = m_index;
        m_index += len;

        err_t err = m_session->send(&HTTP_BODY[index], len);

        if (err != ERR_OK) {
            trace("CommandRequestHandler::sendIndexHtmlBody: this=%p, failed sending body, err[%d]\n", this, err);
            return false;
        }
    }

    return true;
}

bool CommandRequestHandler::onWebSocketData(u8_t *data, size_t len)
{
    uint64_t length = m_websocketHandler.frame_length();
    uint64_t offset = m_websocketHandler.frame_received() - len;

    return (m_ws != nullptr) && m_ws->reader.feed(m_ws->dispatcher, data, len, offset, length);
}

bool CommandRequestHandler::on_sent(u16_t len)
{
    return sendIndexHtmlBody() && HTTPSession::on_sent(len) && ((m_ws == nullptr) || m_ws->dispatcher.dispatch_sent());
}

bool CommandRequestHandler::send_frame(std::string_view prefix, std::string_view topic, const uint8_t *data, uint32_t len)
{
    if (m_ws == nullptr)
    {
        return false;
    }

    char path[128];
    int path_len = snprintf(path, sizeof(path), "%.*s%.*s", (int)prefix.size(), prefix.data(), (int)topic.size(), topic.data());

    if ((path_len < 0) || (path_len >= (int)sizeof(path) - 1))
    {
        return false;
    }

    return sendWebSocketData((const uint8_t *)path, path_len + 1, data, len);
}

bool CommandRequestHandler::send_raw(const uint8_t *data, uint32_t len)
{
    return sendWebSocketData(data, len);
}

uint32_t CommandRequestHandler::link_space()
{
    uint32_t space = send_buffer_size();

    return (space > FRAME_OVERHEAD) ? (space - FRAME_OVERHEAD) : 0;
}

uint32_t WsCommandContext::message_cost(std::string_view topic, uint32_t len) const
{
    // send_space() already takes the frame header off what it reports.
    return len + prefix.size() + topic.size() + 1;
}

bool WsCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    return (handler != nullptr) && handler->send_frame(prefix, topic, data, len);
}

bool WsCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    char total[12];
    int len = snprintf(total, sizeof(total), "%u", (unsigned int)total_bytes);

    return (handler != nullptr) && handler->send_frame(prefix, topic, (const uint8_t *)total, len);
}

uint32_t WsCommandContext::send_space()
{
    return (handler != nullptr) ? handler->link_space() : 0;
}

bool WsCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    return (handler != nullptr) && handler->send_raw(data, len);
}
