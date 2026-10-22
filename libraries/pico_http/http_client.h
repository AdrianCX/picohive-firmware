/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <stdarg.h>

#include "buffer_chain.h"
#include "http_chunk_decoder.h"
#include "http_header.h"
#include "http_request.h"
#include "session.h"

class HTTPClient : public ISessionCallback, private IHttpChunkSink
{
    static const int MAX_REQUEST_SIZE=1024;
    static const uint32_t SEND_PIECE=2048;

public:
    // host has to outlive the client.
    HTTPClient(const char *host, uint16_t port, bool tls);
    virtual ~HTTPClient();

    // Points the client somewhere else, dropping any open connection. False while a request is in flight. host has to outlive the client.
    bool setTarget(const char *host, uint16_t port, bool tls);

    void setHttpCallback(IHttpCallback *callback) { m_callback = callback; }
    void setDebug(bool debug);

    // TCP keepalive for the connections this client opens, see Session::set_keepalive.
    void setKeepAlive(u32_t idle_ms, u32_t interval_ms, u32_t count) { m_connection.set_keepalive(idle_ms, interval_ms, count); }

    bool addWantedHeader(const char *header) { return m_response.addWantedHeader(header); }

    bool begin(const char *command, const char *path);

    bool beginf(const char *command, const char *pathFormat, ...) __attribute__((format(printf, 3, 4)));
    bool vbeginf(const char *command, const char *pathFormat, va_list args);
    bool addHeader(const char *header, const char *value);
    bool send(const u8_t *body = NULL, size_t len = 0);
    bool send_borrowed(const u8_t *body, size_t len);

    bool send(const SharedBuffer& body);
    bool send(const BufferChain& body);

    void update();
    bool busy() const { return m_state != State::Idle; }

    // Not safe to delete while true, see Session::resolving().
    bool resolving() const { return m_connection.resolving(); }

    // What the request in flight is waiting on, for logs.
    const char *stateName() const;

    // Drops the connection. A request in flight completes with ok=false.
    void close();

protected:
    virtual bool on_sent(u16_t len) override;
    virtual bool on_recv(u8_t *data, size_t len) override;
    virtual void on_closed() override;
    virtual void on_connected() override;

private:
    enum class State
    {
        Idle,
        Connecting,
        Waiting,        // request written, nothing back yet
        Header,
        BodyLength,     // Content-Length bytes, or no body at all
        BodyChunked,
        BodyUntilClose, // no length given, the server closing ends it
        Retry,          // a reused connection closed before answering, reconnect from update()
    };

    bool start_request(const char *command, const char *path);
    bool end_request_line(const char *command);
    void connect();
    void write_request();
    void write_more();
    uint32_t piece_at(uint32_t at, const u8_t *&data) const;
    bool start_body();
    bool on_body(u8_t *data, size_t len);
    bool complete(bool extra);
    void finish(bool ok);

    bool onChunkData(uint8_t *data, size_t len) override;

    const char *m_host;
    uint16_t m_port;
    bool m_debug = false;

    State m_state = State::Idle;
    bool m_ready = false;
    bool m_head = false;
    bool m_reused = false;
    bool m_retried = false;
    bool m_drop_pending = false;
    bool m_keep_alive = false;
    uint64_t m_body_left = 0;

    // From begin() until the answer starts coming (a retry sends it again).
    SharedBuffer m_request;

    uint32_t m_sent = 0;
    bool m_all_sent = false;
    bool m_writing = false;

    bool start_send(u8_t *body, size_t len, bool owned);

    u8_t *m_body = NULL;
    size_t m_body_len = 0;
    bool m_body_owned = false;
    SharedBuffer m_body_buffer;
    BufferChain m_body_chain;

    HTTPHeader m_response;
    HTTPChunkDecoder m_chunks;
    IHttpCallback *m_callback = NULL;
    Session m_connection;
};
