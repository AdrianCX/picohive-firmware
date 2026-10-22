/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstddef>
#include <cstdint>

class IHttpChunkSink
{
public:
    virtual ~IHttpChunkSink() {};

    // Decoded body bytes. false stops the decoding with an error.
    virtual bool onChunkData(uint8_t *data, size_t len) = 0;
};

///
/// Decodes a Transfer-Encoding: chunked body as it arrives, split anywhere,
/// handing the data on in place - nothing is buffered. Chunk extensions and
/// trailers are skipped. A bare LF is taken as a line end, like CRLF.
///
class HTTPChunkDecoder
{
public:
    enum class Result
    {
        More,       // all of data used, the body goes on
        Done,       // body and trailers complete; data past used is not part of it
        Error,      // see error()
    };

    void reset();

    // used is how many bytes of data were read: all of them, unless Done.
    Result feed(uint8_t *data, size_t len, size_t& used, IHttpChunkSink& sink);

    const char *error() const { return m_error; }

private:
    enum class State
    {
        Size,
        Extension,      // after the size: ";name=value" up to the line end
        SizeLF,
        Data,
        DataCR,
        DataLF,
        TrailerStart,   // start of a trailer line, or of the empty line ending the body
        TrailerLine,
        TrailerLF,
        Done,
        Error,
    };

    void fail(const char *why);
    void end_size_line();
    void start_size();

    static const uint32_t MAX_LINE = 256;       // longest chunk extension or trailer line
    static const uint32_t MAX_TRAILERS = 1024;  // all trailer lines together

    State m_state = State::Size;
    uint32_t m_left = 0;        // the size while it is read, then what is left of the chunk
    uint32_t m_digits = 0;
    uint32_t m_line = 0;
    uint32_t m_trailers = 0;
    const char *m_error = "";
};
