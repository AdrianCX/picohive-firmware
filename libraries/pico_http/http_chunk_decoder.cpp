/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "http_chunk_decoder.h"
#include "http_header_helper.h"

void HTTPChunkDecoder::reset()
{
    m_state = State::Size;
    m_left = 0;
    m_digits = 0;
    m_line = 0;
    m_trailers = 0;
    m_error = "";
}

void HTTPChunkDecoder::fail(const char *why)
{
    m_state = State::Error;
    m_error = why;
}

void HTTPChunkDecoder::end_size_line()
{
    m_state = (m_left == 0) ? State::TrailerStart : State::Data;
}

void HTTPChunkDecoder::start_size()
{
    m_state = State::Size;
    m_left = 0;
    m_digits = 0;
}

HTTPChunkDecoder::Result HTTPChunkDecoder::feed(uint8_t *data, size_t len, size_t& used, IHttpChunkSink& sink)
{
    size_t i = 0;

    while ((i < len) && (m_state != State::Done) && (m_state != State::Error))
    {
        if (m_state == State::Data)
        {
            size_t bytes = ((len - i) < m_left) ? (len - i) : m_left;

            if (!sink.onChunkData(&data[i], bytes))
            {
                fail("the data was refused");
                break;
            }

            i += bytes;
            m_left -= bytes;

            if (m_left == 0)
            {
                m_state = State::DataCR;
            }
            continue;
        }

        if ((m_state == State::TrailerStart) || (m_state == State::TrailerLine) || (m_state == State::TrailerLF))
        {
            if (++m_trailers > MAX_TRAILERS)
            {
                fail("trailers too long");
                break;
            }
        }

        uint8_t c = data[i++];

        switch (m_state)
        {
            case State::Size:
            {
                int digit = HTTPHeaderHelper::hexValue(c);

                if (digit >= 0)
                {
                    if (m_left > (UINT32_MAX >> 4))
                    {
                        fail("chunk size too large");
                    }
                    else
                    {
                        m_left = (m_left << 4) | digit;
                        ++m_digits;
                    }
                }
                else if (m_digits == 0)
                {
                    fail("chunk size missing");
                }
                else if (c == '\r')
                {
                    m_state = State::SizeLF;
                }
                else if (c == '\n')
                {
                    end_size_line();
                }
                else if ((c == ';') || (c == ' ') || (c == '\t'))
                {
                    m_state = State::Extension;
                    m_line = 1;
                }
                else
                {
                    fail("bad character in chunk size");
                }
                break;
            }
            case State::Extension:
            {
                if (c == '\r')
                {
                    m_state = State::SizeLF;
                }
                else if (c == '\n')
                {
                    end_size_line();
                }
                else if (++m_line > MAX_LINE)
                {
                    fail("chunk extension too long");
                }
                break;
            }
            case State::SizeLF:
            {
                if (c == '\n')
                {
                    end_size_line();
                }
                else
                {
                    fail("chunk size line not ended by CRLF");
                }
                break;
            }
            case State::DataCR:
            {
                if (c == '\r')
                {
                    m_state = State::DataLF;
                }
                else if (c == '\n')
                {
                    start_size();
                }
                else
                {
                    fail("chunk data not followed by CRLF");
                }
                break;
            }
            case State::DataLF:
            {
                if (c == '\n')
                {
                    start_size();
                }
                else
                {
                    fail("chunk data not followed by CRLF");
                }
                break;
            }
            case State::TrailerStart:
            {
                if (c == '\r')
                {
                    m_state = State::TrailerLF;
                }
                else if (c == '\n')
                {
                    m_state = State::Done;
                }
                else
                {
                    m_state = State::TrailerLine;
                    m_line = 1;
                }
                break;
            }
            case State::TrailerLine:
            {
                if (c == '\n')
                {
                    m_state = State::TrailerStart;
                }
                else if (++m_line > MAX_LINE)
                {
                    fail("trailer line too long");
                }
                break;
            }
            case State::TrailerLF:
            {
                if (c == '\n')
                {
                    m_state = State::Done;
                }
                else
                {
                    fail("trailers not ended by CRLF");
                }
                break;
            }
            default:
            {
                break;
            }
        }
    }

    used = i;

    if (m_state == State::Done)
    {
        return Result::Done;
    }

    return (m_state == State::Error) ? Result::Error : Result::More;
}
