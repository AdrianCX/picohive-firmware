/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <stdarg.h>

#include "http_session.h"


class IHttpCallback
{
public:
    virtual ~IHttpCallback() {};
    
    virtual bool onHeaderReceived(HTTPHeader& header) { return true; };
    virtual bool onHttpData(u8_t *data, size_t len) { return true; }
    
    virtual void onRequestDestroyed() {};
    virtual void onResponseComplete(bool ok) {};
};

class HTTPRequest : public ISessionCallback
{
    static const int MAX_HEADER_SIZE=2048;
    
public:
    HTTPRequest(const char *host,
                uint16_t port,
                bool tls,
                const char *command,
                const char *path);

    static HTTPRequest *create(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, ...) __attribute__((format(printf, 5, 6)));

    bool addHeader(const char *header, const char* value);
    bool setBody(u8_t *data, size_t len);
    void setDebug(bool debug);

    bool send();

    void setHttpCallback(IHttpCallback *callback) { m_callback = callback; }

    // Only return headers that have been added here for space concerns
    bool addWantedHeader(const char *header) { return m_response.addWantedHeader(header); }
    
protected:
    virtual bool on_sent(u16_t len) override { return true; };
    virtual bool on_recv(u8_t *data, size_t len) override;
    virtual void on_closed() override;
    virtual void on_connected() override;

private:
    HTTPRequest() {}
    virtual ~HTTPRequest();

    void init(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, ...) __attribute__((format(printf, 6, 7)));
    void vinit(const char *host, uint16_t port, bool tls, const char *command, const char *pathFormat, va_list args);
    
    HTTPSessionState m_state = HTTPSessionState::INIT;

    bool m_debug = false;
    uint16_t m_port = 443;
    uint16_t m_headerStart = 0;
    uint16_t m_headerIndex = 0;
    
    char m_header[MAX_HEADER_SIZE+2];

    HTTPHeader m_response;

    u8_t  *m_body = NULL;
    size_t m_bodyLen = 0;

    IHttpCallback *m_callback = NULL;

    bool m_sending = false;
    bool m_closed = false;

    Session m_connection;
};
