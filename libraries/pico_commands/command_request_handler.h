/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "command_dispatcher.h"
#include "command_registration.h"
#include "http_session.h"
#include "mcp_server.h"
#include "ws_frame_reader.h"

class CommandRequestHandler;

class WsCommandContext : public CommandContext
{
public:
    WsCommandContext() { set_queue(m_waiting); }

    CommandRequestHandler *handler = nullptr;

    uint32_t send_space() override;
    uint32_t message_cost(std::string_view topic, uint32_t len) const override;

protected:
    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;

private:
    ReplyBuffer m_waiting[COMMAND_REPLY_QUEUE_LENGTH];
};

class CommandRequestHandler
    : public HTTPSession
    , public McpWaiter
{
public:
    static void set_config(const uint8_t *http_body, unsigned int body_len);
    static void create(void *arg, bool tls);

    static void require_auth(bool required);

    CommandRequestHandler(void *arg, bool tls);
    virtual ~CommandRequestHandler();

    bool send_frame(std::string_view prefix, std::string_view topic, const uint8_t *data, uint32_t len);
    bool send_raw(const uint8_t *data, uint32_t len);
    uint32_t link_space();

    void mcp_done() override;

private:
    bool onRequestReceived(HTTPHeader& header) override;
    bool onHttpData(u8_t *data, size_t len) override;
    bool onWebSocketData(u8_t *data, size_t len) override;
    bool on_sent(u16_t len) override;

    bool onMcpRequest(HTTPHeader& header);
    bool answerMcp();
    bool sendMcpAnswer(const char *status);

    bool sendEmptyReply(const char *status, const char *extra_headers = "");
    bool sendReply(const char *status, const char *headers, const uint8_t *body, uint32_t body_len);
    bool sendBody();
    bool isAuthorized(HTTPHeader& header);
    bool isSameOrigin(HTTPHeader& header);

    const uint8_t *m_body = nullptr;
    uint32_t m_body_len = 0;
    uint32_t m_index = 0;

    struct McpState
    {
        SharedBuffer request;
        uint32_t expected = 0;
        SharedBuffer answer;
        bool waiting = false;
    };

    McpState *m_mcp = nullptr;

    struct WebSocketState
    {
        WebSocketState(CommandRequestHandler *handler);

        WsCommandContext context;
        CommandSlot slots[1];
        CommandDispatcher dispatcher;
        WsFrameReader reader;
    };

    WebSocketState *m_ws = nullptr;
};
