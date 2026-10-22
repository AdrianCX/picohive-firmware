/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#ifndef HTTP_HEADER_H
#define HTTP_HEADER_H

#include "os_platform.h"
#include "shared_buffer.h"

/// Simple HTTP Header parser that uses provided data in place, no allocations or extra buffers. All pointers live as long as provided data.
class HTTPHeader
{
    static const int MAX_HEADERS=10;
    static const int MAX_HEADER_SIZE=2048;
public:
    HTTPHeader();

    // Only return headers that have been added here for space concerns
    bool addWantedHeader(const char *header);

    // Parse a given chunk - caller should check isFinished before reading any processed data
    int32_t parse(char *data, int len);

    // Ready to parse the next message, keeping the wanted headers.
    void reset();

    // reset() and release buffers, getters are unsafe
    void release();

    // Access functions to parsed data
    const char *getCommand() { return isRequest() ? m_text : NULL; }
    const char *getPath() { return isRequest() ? m_path : NULL; }
    uint16_t getResponseCode() { return isResponse() ? m_responseCode : 0; }
    const char *getVersion() { return isResponse() ? m_text : NULL; }

    // return data only if header was requested via addWantedHeader and is present
    const char *getHeaderValue(const char *headerName);

    void print();

    bool isRequest() { return m_isRequest; }
    bool isResponse() { return !m_isRequest; }
    bool isFinished() { return m_state == STATE_FINISHED; }
    bool isFailed() { return m_state == STATE_FAILED; }

    uint32_t bufferUsed() { return m_index; }
    
private:
    bool parse_chunk(char *data, int& len, int& i);

    enum State
    {
        STATE_FIRST,
        STATE_CMDTYPE,
        STATE_SECOND,
        STATE_SKIP,
        STATE_NAME,
        STATE_SKIP_SPACES,
        STATE_VALUE,
        STATE_FAILED,
        STATE_FINISHED,
    };

    bool m_isRequest = false;
    uint8_t m_numHeaders = 0;
    uint8_t m_currentHeader = 0;
    uint16_t m_responseCode = 0;
    uint16_t m_start = 0;
    uint16_t m_index = 0;
    // MAX_HEADER_SIZE bytes from the heap, only while there is a header.
    SharedBuffer m_buffer;
    char *m_text = nullptr;
    char *m_path = NULL;

    const char *m_headerName[MAX_HEADERS] = { 0 };
    const char *m_headerValue[MAX_HEADERS] = { 0 };

    State m_state = STATE_FIRST;
};

#endif
