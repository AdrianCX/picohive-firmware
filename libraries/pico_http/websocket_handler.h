/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include "os_platform.h"

#define MAX_WEBSOCKET_HEADER 16

enum WebSocketState
{
    WAIT_PACKET,
    WAIT_DATA,
};

enum WebSocketOperation
{
    CONTINUATION_FRAME = 0x00,
    TEXT_FRAME         = 0x01,
    BINARY_FRAME       = 0x02,
    CONNECTION_CLOSE   = 0x08,
    PING               = 0x09,
    PONG               = 0x0A,
};

class WebSocketInterface
{
public:
    virtual bool onWebSocketData(uint8_t *data, size_t len) = 0;
    virtual bool onWebsocketEncodedData(const uint8_t *data, size_t len) = 0;
    virtual bool onFinishedPacket() { return true; }
};

class WebSocketHandler
{
public:
    WebSocketHandler() {};

    ///
    /// This will decode messages in-place in provided data and trigger callback with them.
    /// It will cache any remaining bytes that are part of a next message header.
    ///
    /// @returns - true - if all data consumed succesfully.
    ///          - false - on failure, connection must be closed.
    ///
    bool decodeData(uint8_t* data, size_t len, WebSocketInterface *callback);

    ///
    /// This will encode data in a message and call back with bytes to send.
    ///
    /// @returns - true - if all data processed succesfully.
    ///          - false - on failure, connection must be closed.
    ///
    bool encodeData(const uint8_t* data, size_t len, WebSocketInterface *callback);

    ///
    /// The same, for a payload that sits in two places - a few bytes of the
    /// caller's in front, a body somewhere else - sent as one frame without
    /// copying either.
    ///
    bool encodeData(const uint8_t* head, size_t head_len, const uint8_t* body, size_t body_len, WebSocketInterface *callback);

    ///
    /// Payload size of the frame being decoded, and how much of it has
    /// arrived so far. A receiver that streams needs the first to know what
    /// it is in for and the second to recognise the last chunk.
    ///
    uint64_t frame_length() const { return m_webSocketDataLen; }
    uint64_t frame_received() const { return m_webSocketDataIndex; }

private:
    bool m_fin = false;
    bool m_mask = false;
    WebSocketOperation m_webSocketOperation;
    uint64_t m_webSocketDataLen = 0;
    uint64_t m_webSocketDataIndex = 0;
    uint8_t m_maskingKey[4];

    WebSocketState m_state = WebSocketState::WAIT_PACKET;
    uint16_t m_bufferIndex = 0;
    uint8_t m_buffer[MAX_WEBSOCKET_HEADER];
};

#endif
