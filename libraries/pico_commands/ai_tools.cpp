/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "ai_tools.h"
#include "command_dispatcher.h"

const char *AiTools::s_instructions = NULL;
AiTools::Ready AiTools::s_ready = NULL;
AiTools::Busy AiTools::s_busy = NULL;

uint32_t AiTools::tool_name(const Command& command, char *out, uint32_t size)
{
    if (command.name.size() >= size)
    {
        return 0;
    }

    for (uint32_t i = 0; i < command.name.size(); ++i)
    {
        char c = command.name[i];
        bool kept = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '-');
        out[i] = kept ? c : '_';
    }

    out[command.name.size()] = 0;
    return command.name.size();
}

const Command *AiTools::find_tool(const char *name)
{
    char offered[72];

    for (const Command& command : AiCommandSet::all())
    {
        if ((tool_name(command, offered, sizeof(offered)) > 0) && (strcmp(offered, name) == 0))
        {
            return &command;
        }
    }

    return NULL;
}

bool AiTools::make_topic(const Command& command, const char *args, char *out, uint32_t size)
{
    while (*args == '/')
    {
        ++args;
    }

    if (!strncmp(args, command.name.data(), command.name.size()) && (args[command.name.size()] == '/'))
    {
        args += command.name.size() + 1;
    }

    if (strlen(args) >= MAX_COMMAND_ARGS)
    {
        return false;
    }

    int len = snprintf(out, size, "%.*s%s%s", (int)command.name.size(), command.name.data(), (args[0] != 0) ? "/" : "", args);

    return (len > 0) && ((uint32_t)len < size);
}

bool AiTools::done_after(uint64_t since_us, uint64_t timeout_us)
{
    if (s_ready == NULL)
    {
        return true;
    }

    return (since_us >= AI_SETTLE_US) && (s_ready() || (since_us >= timeout_us));
}

bool AiTools::finished_after(uint64_t since_us, uint64_t timeout_us, uint64_t busy_timeout_us)
{
    if ((s_busy != NULL) && s_busy())
    {
        return since_us >= busy_timeout_us;
    }

    return done_after(since_us, timeout_us);
}
