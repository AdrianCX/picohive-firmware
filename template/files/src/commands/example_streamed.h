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

#pragma once

#include "command_registration.h"

// Any size, in chunks as they arrive. One instance per message.
// Retained over MQTT: add clears_retained = true so it does not run again on reconnect.
struct ExampleStreamed
{
    static constexpr std::string_view name = "example-streamed";
    static constexpr std::string_view help = "example-streamed - counts the bytes of a body of any size";

    static constexpr Payload payload = Payload::Streamed;
    static constexpr uint32_t max_bytes = 1024 * 1024;

    bool on_header(CommandContext& ctx);
    bool on_chunk(CommandContext& ctx, const uint8_t *data, uint32_t len, bool last_chunk);

    void on_abort(CommandContext& ctx, const char *reason);

private:
    uint32_t m_received = 0;
};
