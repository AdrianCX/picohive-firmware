/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mbedtls/base64.h"
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

    if (m_mcp != nullptr)
    {
        if (m_mcp->waiting)
        {
            McpServer::get().cancel(this);
        }

        delete m_mcp;
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

bool CommandRequestHandler::isSameOrigin(HTTPHeader& header)
{
    const char *origin = header.getHeaderValue("Origin");
    const char *host = header.getHeaderValue("Host");
    const char *origin_host = (origin != nullptr) ? strstr(origin, "://") : nullptr;

    if (PicoHelper::is_domain(host) || ((origin != nullptr) && ((origin_host == nullptr) || (strcasecmp(origin_host + 3, host) != 0))))
    {
        trace("CommandRequestHandler::isSameOrigin: this=%p, refused, origin[%s] host[%s]", this, origin, safestr(host));
        return false;
    }

    return true;
}

bool CommandRequestHandler::onRequestReceived(HTTPHeader& header)
{
    if (AUTH_REQUIRED)
    {
        if (!isAuthorized(header))
        {
            return sendEmptyReply("401 Unauthorized", "WWW-Authenticate: Basic realm=\"PicoServer\"\r\n");
        }
    }

    if (strcmp(header.getPath(), MCP_PATH) == 0)
    {
        return onMcpRequest(header);
    }

    if (strcmp(header.getCommand(), "GET") == 0)
    {
        if (strcmp(header.getPath(), "/websocket") == 0)
        {
            if (!isSameOrigin(header))
            {
                return sendEmptyReply("403 Forbidden");
            }

            trace("CommandRequestHandler::onRequestReceived: this=%p, acceptWebSocket.", this);
            m_ws = new WebSocketState(this);
            return acceptWebSocket(header);
        }

        trace("CommandRequestHandler::onRequestReceived: this=%p, sendReply[index.html]:", this);

        return sendReply("200 OK", "Content-Type: text/html; charset=UTF-8\r\nContent-Encoding: gzip\r\n", HTTP_BODY, HTTP_BODY_LEN);
    }

    return false;
}

bool CommandRequestHandler::sendReply(const char *status, const char *headers, const uint8_t *body, uint32_t body_len)
{
    char reply[192];
    int n = snprintf(reply, sizeof(reply), "HTTP/1.1 %s\r\n%sConnection: close\r\nContent-Length: %u\r\n\r\n", status, headers, (unsigned int)body_len);

    if ((n < 0) || (n >= (int)sizeof(reply)))
    {
        trace("CommandRequestHandler::sendReply: this=%p, buffer to small, expected[%d] got[%d]:\n", this, n, (int)sizeof(reply));
        return false;
    }

    if (!sendHttpReplyRaw(reply, n, true, body_len))
    {
        return false;
    }

    m_body = body;
    m_body_len = body_len;
    m_index = 0;
    return sendBody();
}

bool CommandRequestHandler::sendEmptyReply(const char *status, const char *extra_headers)
{
    return sendReply(status, extra_headers, nullptr, 0);
}

bool CommandRequestHandler::sendBody()
{
    if ((m_index < m_body_len) && (send_buffer_size() > TCP_MSS))
    {
        uint32_t socket_buffer_size = send_buffer_size() - TCP_MSS;
        uint32_t remaining = m_body_len - m_index;
        uint32_t len = (socket_buffer_size < remaining) ? socket_buffer_size : remaining;

        uint32_t index = m_index;
        m_index += len;

        err_t err = m_session->send(&m_body[index], len);

        if (err != ERR_OK) {
            trace("CommandRequestHandler::sendBody: this=%p, failed sending body, err[%d]\n", this, err);
            return false;
        }
    }

    return true;
}

bool CommandRequestHandler::onMcpRequest(HTTPHeader& header)
{
    if (!get_config().mcp_enabled)
    {
        trace("CommandRequestHandler::onMcpRequest: this=%p, mcp is off", this);
        return sendEmptyReply("404 Not Found");
    }

    if ((header.getHeaderValue("Origin") != nullptr) && !isSameOrigin(header))
    {
        return sendEmptyReply("403 Forbidden");
    }

    if (strcmp(header.getCommand(), "POST") != 0)
    {
        return sendEmptyReply("405 Method Not Allowed", "Allow: POST\r\n");
    }

    const char *length = header.getHeaderValue("Content-Length");
    char *end = nullptr;
    unsigned long expected = (length != nullptr) ? strtoul(length, &end, 10) : 0;

    if ((length == nullptr) || (end == length) || (expected == 0))
    {
        return sendEmptyReply("411 Length Required");
    }

    if (expected > MCP_MAX_REQUEST)
    {
        return sendEmptyReply("413 Content Too Large");
    }

    m_mcp = new McpState();
    m_mcp->request = SharedBuffer(expected);
    m_mcp->expected = expected;

    if (!m_mcp->request)
    {
        return sendEmptyReply("503 Service Unavailable");
    }

    return true;
}

bool CommandRequestHandler::onHttpData(u8_t *data, size_t len)
{
    if (m_mcp == nullptr)
    {
        return HTTPSession::onHttpData(data, len);
    }

    if (!m_mcp->request)
    {
        return true;
    }

    uint32_t room = m_mcp->expected - m_mcp->request.size();
    m_mcp->request.append(data, (len < room) ? len : room);

    return (m_mcp->request.size() < m_mcp->expected) || answerMcp();
}

bool CommandRequestHandler::answerMcp()
{
    McpOutcome outcome = McpServer::get().handle(m_mcp->request.c_str(), m_mcp->request.size(), m_mcp->answer);
    m_mcp->request = SharedBuffer();

    switch (outcome)
    {
        case McpOutcome::Accepted:
            return sendEmptyReply("202 Accepted");

        case McpOutcome::Bad:
            if (!m_mcp->answer)
            {
                return sendEmptyReply("503 Service Unavailable");
            }

            return sendMcpAnswer("400 Bad Request");

        case McpOutcome::AnswerWhenDone:
            if (McpServer::get().wait(this))
            {
                m_mcp->waiting = true;
                return true;
            }

            [[fallthrough]];

        case McpOutcome::Answer:
            break;
    }

    return sendMcpAnswer("200 OK");
}

bool CommandRequestHandler::sendMcpAnswer(const char *status)
{
    return sendReply(status, "Content-Type: application/json\r\n", (const uint8_t *)m_mcp->answer.c_str(), m_mcp->answer.size());
}

void CommandRequestHandler::mcp_done()
{
    m_mcp->waiting = false;

    if (!sendMcpAnswer("200 OK"))
    {
        close();
    }
}

bool CommandRequestHandler::onWebSocketData(u8_t *data, size_t len)
{
    uint64_t length = m_websocketHandler.frame_length();
    uint64_t offset = m_websocketHandler.frame_received() - len;

    return (m_ws != nullptr) && m_ws->reader.feed(m_ws->dispatcher, data, len, offset, length);
}

bool CommandRequestHandler::on_sent(u16_t len)
{
    return sendBody() && HTTPSession::on_sent(len) && ((m_ws == nullptr) || m_ws->dispatcher.dispatch_sent());
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
