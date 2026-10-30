/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "capture_command_context.h"
#include "pico_mjson.h"
#include "shared_buffer.h"

#define MCP_PATH "/mcp"

#ifndef MCP_MAX_REQUEST
#define MCP_MAX_REQUEST 4096
#endif

#ifndef MCP_TOOL_REPLY_BYTES
#define MCP_TOOL_REPLY_BYTES 3072
#endif

#ifndef MCP_MAX_REPLY
#define MCP_MAX_REPLY 8192
#endif

#ifndef MCP_READY_TIMEOUT_US
#define MCP_READY_TIMEOUT_US 30000000ULL
#endif

#ifndef MCP_PLAN_TIMEOUT_US
#define MCP_PLAN_TIMEOUT_US 180000000ULL
#endif

#define MCP_MAX_WAITING 4

enum class McpOutcome : uint8_t
{
    Answer,
    AnswerWhenDone,
    Accepted,
    Bad,
};

class McpWaiter
{
public:
    virtual void mcp_done() = 0;
};

class McpCommandContext : public CaptureCommandContext
{
public:
    SharedBuffer text;

protected:
    void take(const char *text, uint32_t len) override;
};

class McpServer
{
public:
    static McpServer& get();

    McpOutcome handle(const char *request, uint32_t len, SharedBuffer& answer);

    bool wait(McpWaiter *waiter);

    void cancel(McpWaiter *waiter);

    void update(uint64_t now_us);

private:
    McpServer() = default;

    void initialize(const JsonView& request, JsonWriter& out);
    void list_tools(JsonWriter& out);
    bool call_tool(const JsonView& request, JsonWriter& out, bool& ran);

    void begin(JsonWriter& out, const JsonView& id);
    void error(JsonWriter& out, int code, const char *message);
    void add_tool(JsonWriter& out, const char *name, const char *description, bool args, bool body = false);
    void add_text(JsonWriter& out, const char *text, bool is_error);

    struct Waiting
    {
        McpWaiter *waiter = nullptr;
        uint64_t since_us = 0;
    };

    Waiting m_waiting[MCP_MAX_WAITING];
    uint64_t m_now_us = 0;

    McpCommandContext m_command;
};
