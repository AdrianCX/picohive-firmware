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

// Small body, handed over whole. Bigger than max_bytes is refused.
struct ExampleBuffered
{
    static constexpr std::string_view name = "example-buffered";
    static constexpr std::string_view help = "example-buffered - answers with the size of the body";

    static constexpr Payload payload = Payload::Buffered;
    static constexpr uint32_t max_bytes = 256;

    bool on_payload(CommandContext& ctx, std::string_view body);
};
