/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "ai_tools.h"
#include "mcp_server.h"
#include "pico_logger.h"

static const char *PROTOCOL_VERSIONS[] = { "2025-11-25", "2025-06-18", "2025-03-26" };

static const char *BASE_INSTRUCTIONS =
    "The tools are the board's own commands. A tool's args are what follows its name in its description, "
    "without the name: for move/<joint>/<deg>, args is elbow/45. Call context first: it is the state of things.";

static const char *WAITS =
    " A call answers once what it started has finished.";

static const char *CONTEXT_TOOL = "context";
static const char *CONTEXT_DESCRIPTION = "the state of things right now - read it before anything else";

static const char *ARGS_DESCRIPTION = "what follows the tool's name in its description, e.g. elbow/45 for move/<joint>/<deg>";

static const char *BODY_DESCRIPTION = "the command's body, as its description says - what is longer than args can hold";

void McpCommandContext::take(const char *data, uint32_t len)
{
    text.append(data, len);
}

McpServer& McpServer::get()
{
    static McpServer instance;
    return instance;
}

McpOutcome McpServer::handle(const char *request, uint32_t len, SharedBuffer& answer)
{
    answer = SharedBuffer(MCP_MAX_REPLY);

    if (!answer)
    {
        return McpOutcome::Bad;
    }

    JsonView rpc(request, len);
    JsonView id;
    char method[32] = {};
    McpOutcome outcome = McpOutcome::Answer;

    {
        JsonWriter out(answer.data(), answer.capacity());

        if (!rpc.is_valid() || (rpc.type() != JsonType::Object))
        {
            begin(out, id);
            error(out, -32700, "not one JSON-RPC object");
            outcome = McpOutcome::Bad;
        }
        else
        {
            JsonType id_type = rpc.type("$.id");

            if ((id_type != JsonType::String) && (id_type != JsonType::Number))
            {
                return McpOutcome::Accepted;
            }

            id = rpc.find("$.id");
            begin(out, id);

            if (!rpc.get_whole_string("$.method", method, sizeof(method)))
            {
                error(out, -32600, "no method");
                outcome = McpOutcome::Bad;
            }
            else if (strcmp(method, "initialize") == 0)
            {
                initialize(rpc, out);
            }
            else if (strcmp(method, "ping") == 0)
            {
                out.begin_object("result").end_object();
            }
            else if (strcmp(method, "tools/list") == 0)
            {
                list_tools(out);
            }
            else if (strcmp(method, "tools/call") == 0)
            {
                bool ran = false;

                if (call_tool(rpc, out, ran) && ran && AiTools::has_ready())
                {
                    outcome = McpOutcome::AnswerWhenDone;
                }
            }
            else
            {
                error(out, -32601, "method not found");
            }
        }

        out.end_object();

        if (out.ok())
        {
            answer.set_size(out.length());
            return outcome;
        }
    }

    trace("mcp: the answer to %s does not fit in %u bytes", method, (unsigned int)MCP_MAX_REPLY);

    JsonWriter out(answer.data(), answer.capacity());
    begin(out, id);
    error(out, -32603, "the answer does not fit");
    out.end_object();
    answer.set_size(out.length());

    return outcome;
}

void McpServer::begin(JsonWriter& out, const JsonView& id)
{
    out.begin_object().add_string("jsonrpc", "2.0").add_raw("id", id);
}

void McpServer::error(JsonWriter& out, int code, const char *message)
{
    out.begin_object("error").add_int("code", code).add_string("message", message).end_object();
}

void McpServer::initialize(const JsonView& request, JsonWriter& out)
{
    const char *version = PROTOCOL_VERSIONS[0];
    char asked[16] = {};

    if (request.get_whole_string("$.params.protocolVersion", asked, sizeof(asked)))
    {
        for (const char *known : PROTOCOL_VERSIONS)
        {
            if (strcmp(asked, known) == 0)
            {
                version = known;
            }
        }
    }

    trace("mcp: initialize, asked for %s, answering %s", asked, version);

    out.begin_object("result")
       .add_string("protocolVersion", version)
       .begin_object("capabilities").begin_object("tools").end_object().end_object()
       .begin_object("serverInfo").add_string("name", "picohive").add_string("version", "1.0").end_object()
       .add_fstring("instructions", "%s%s%s%s", BASE_INSTRUCTIONS, AiTools::has_ready() ? WAITS : "",
                    (AiTools::instructions() != NULL) ? " " : "", (AiTools::instructions() != NULL) ? AiTools::instructions() : "")
       .end_object();
}

void McpServer::list_tools(JsonWriter& out)
{
    out.begin_object("result").begin_array("tools");

    if (AiContextCommandSet::size() > 0)
    {
        add_tool(out, CONTEXT_TOOL, CONTEXT_DESCRIPTION, false);
    }

    char name[72];
    char help[160];

    for (const Command& command : AiCommandSet::all())
    {
        if (AiTools::tool_name(command, name, sizeof(name)) > 0)
        {
            snprintf(help, sizeof(help), "%.*s", (int)command.help.size(), command.help.data());
            add_tool(out, name, help, true, command.payload == Payload::Buffered);
        }
    }

    out.end_array().end_object();
}

void McpServer::add_tool(JsonWriter& out, const char *name, const char *description, bool args, bool body)
{
    out.begin_object().add_string("name", name).add_string("description", description).begin_object("inputSchema").add_string("type", "object");

    if (args)
    {
        out.begin_object("properties").begin_object("args").add_string("type", "string").add_string("description", ARGS_DESCRIPTION).end_object();

        if (body)
        {
            out.begin_object("body").add_string("type", "string").add_string("description", BODY_DESCRIPTION).end_object();
        }

        out.end_object();
    }

    out.end_object().end_object();
}

void McpServer::add_text(JsonWriter& out, const char *text, bool is_error)
{
    out.begin_object("result")
       .begin_array("content").begin_object().add_string("type", "text").add_string("text", text).end_object().end_array()
       .add_bool("isError", is_error)
       .end_object();
}

bool McpServer::call_tool(const JsonView& request, JsonWriter& out, bool& ran)
{
    char name[72] = {};
    char args[MAX_COMMAND_ARGS + 8] = {};

    if (!request.get_whole_string("$.params.name", name, sizeof(name)))
    {
        error(out, -32602, "no tool name");
        return false;
    }

    bool args_fit = (request.type("$.params.arguments.args") != JsonType::String) ||
                    request.get_whole_string("$.params.arguments.args", args, sizeof(args));

    SharedBuffer body;

    if (request.type("$.params.arguments.body") == JsonType::String)
    {
        body = SharedBuffer(request.length());

        if (!body || !request.get_whole_string("$.params.arguments.body", body.data(), body.capacity() + 1))
        {
            error(out, -32603, "no memory for the body");
            return false;
        }

        body.set_size(strlen(body.c_str()));
    }

    m_command.text = SharedBuffer(MCP_TOOL_REPLY_BYTES);

    if (!m_command.text)
    {
        error(out, -32603, "no memory for the answer");
        return false;
    }

    if (strcmp(name, CONTEXT_TOOL) == 0)
    {
        m_command.run_each(AiContextCommandSet::all(), MCP_TOOL_REPLY_BYTES);
        trace("mcp: context, %u bytes", (unsigned int)m_command.text.size());
        add_text(out, m_command.text.c_str(), false);
        m_command.text = SharedBuffer();
        return true;
    }

    const Command *command = AiTools::find_tool(name);
    char topic[MAX_COMMAND_ARGS + 72];

    if (command == NULL)
    {
        trace("mcp: call %s(%s): no such tool", name, args);
        error(out, -32602, "no such tool");
    }
    else if (!args_fit || !AiTools::make_topic(*command, args, topic, sizeof(topic)))
    {
        trace("mcp: call %s: args too long", name);
        add_text(out, AiTools::REFUSED, true);
    }
    else
    {
        m_command.run(AiCommandSet::all(), topic, MCP_TOOL_REPLY_BYTES, body.view());
        trace("mcp: call %s(%s), ran %s with %u bytes of body: %.200s", name, args, topic, (unsigned int)body.size(),
              m_command.text.c_str());
        add_text(out, m_command.text.empty() ? AiTools::NO_ANSWER : m_command.text.c_str(), false);
        ran = true;
    }

    m_command.text = SharedBuffer();
    return command != NULL;
}

bool McpServer::wait(McpWaiter *waiter)
{
    for (Waiting& waiting : m_waiting)
    {
        if (waiting.waiter == nullptr)
        {
            waiting.waiter = waiter;
            waiting.since_us = m_now_us;
            return true;
        }
    }

    return false;
}

void McpServer::cancel(McpWaiter *waiter)
{
    for (Waiting& waiting : m_waiting)
    {
        if (waiting.waiter == waiter)
        {
            waiting.waiter = nullptr;
        }
    }
}

void McpServer::update(uint64_t now_us)
{
    m_now_us = now_us;

    for (Waiting& waiting : m_waiting)
    {
        if ((waiting.waiter != nullptr) && AiTools::finished_after(now_us - waiting.since_us, MCP_READY_TIMEOUT_US, MCP_PLAN_TIMEOUT_US))
        {
            McpWaiter *waiter = waiting.waiter;
            waiting.waiter = nullptr;

            if ((now_us - waiting.since_us) >= MCP_READY_TIMEOUT_US)
            {
                trace("mcp: still not done after %u s, answering", (unsigned int)((now_us - waiting.since_us) / 1000000));
            }

            waiter->mcp_done();
        }
    }
}
