/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef HTTP_SESSION_H
#define HTTP_SESSION_H

#include "os_platform.h"

#include "session.h"
#include "http_header.h"
#include "websocket_handler.h"

enum HTTPSessionState
{
    INIT,
    HEADER_RECEIVED,
    WEBSOCKET_ESTABLISHED,
    FAIL,
};

class HTTPSession
    : public WebSocketInterface
    , public ISessionCallback
{
public:
    static void create(void *arg, bool tls);

    virtual bool onRequestReceived(HTTPHeader& header) { return false; };
    virtual bool onHttpData(u8_t *data, size_t len) { return false; }

    virtual bool onWebSocketData(u8_t *data, size_t len) override { return false; }
    virtual bool onWebsocketEncodedData(const uint8_t *data, size_t len) override;
    
    bool acceptWebSocket(HTTPHeader& header);
    
    bool sendHttpReplyRaw(const char *reply, size_t reply_len, bool closeOnSend = false, size_t bodyToFollow = 0);
    bool sendHttpReply(const char *extra_headers, const char *body, int body_len);
    bool sendWebSocketData(const uint8_t *body, int body_len);
    bool sendWebSocketData(const uint8_t *head, int head_len, const uint8_t *body, int body_len);

    virtual bool on_recv(u8_t *data, size_t len) override;
    virtual bool on_sent(u16_t len) override;
    virtual void on_closed() override;

    void close();
    u16_t send_buffer_size();

    void set_debug(bool debug) { m_session->set_debug(debug); }

    // Only return headers that have been added here for space concerns
    bool addWantedHeader(const char *header) { return (m_header != NULL) && m_header->addWantedHeader(header); }
    
protected:
    HTTPSession(void *arg, bool tls);
    virtual ~HTTPSession();
    
protected:
    HTTPSessionState m_state;
    HTTPHeader *m_header;
    WebSocketHandler m_websocketHandler;
    Session *m_session;

    bool m_closeOnSend = false;
    int32_t m_pendingBytes = 0;
};

#endif
