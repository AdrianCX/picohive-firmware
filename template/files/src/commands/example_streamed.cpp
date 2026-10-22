/* SPDX-License-Identifier: MIT

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "example_streamed.h"
#include "pico_logger.h"

bool ExampleStreamed::on_header(CommandContext& ctx)
{
    // ctx.message_length is the whole body.
    return true;
}

bool ExampleStreamed::on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk)
{
    m_received += len;

    if (!last_chunk)
    {
        return true;
    }

    return ctx.reply_fmt(32, "%u of %u bytes", (unsigned int)m_received, (unsigned int)ctx.message_length);
}

void ExampleStreamed::on_abort(CommandContext& ctx, const char *reason)
{
    trace("example-streamed: cut off after %u bytes: %s", (unsigned int)m_received, reason);
}

MQTT_COMMAND(ExampleStreamed);
WS_COMMAND(ExampleStreamed);
