# Commands

[← Back](../README.md)

- Goal is to split off apps and os as much as possible
- One way is to have simple lego-pieces style commands
- These do not need to know about protocols calling them

## How these are structured

Header:

```cpp
#pragma once
#include "command_registration.h"

struct Led
{
    static constexpr std::string_view name = "led";
    static constexpr std::string_view help = "led/<on|off> - switch the LED";
    static constexpr std::string_view mqtt_prefix = "send/";
    static constexpr std::string_view ws_prefix = "/rc/";

    static constexpr Payload payload = Payload::None;

    bool on_header(CommandContext& ctx);
};
```

Implementation:

```cpp
#include "led.h"

bool Led::on_header(CommandContext& ctx)
{
    bool on = (ctx.args == "on");
    // ... do work ...
    return ctx.reply_fmt(8, "led %s", on ? "on" : "off");
}

MQTT_COMMAND(Led);
WS_COMMAND(Led);
```

## How they work

- Every .cpp in src/commands/ gets built, and the macros register the command at link time

MQTT:
- MQTT messages sent to devices/<id>/send/led/on will invoke command.
- Responses from command get sent back to devices/<id>/recv/led

Websocket:
- Web page sends a WebSocket frame starting with /rc/led/on and followed by args.
- That gets routed to this same command. Replies get sent back to the page.
- Returning false drops the connection.

Telegram/Calendar work in a similar manner.

## Bigger payloads

Command above has everthing it needs in the topic, change payload type:

- **Payload::Buffered**: small commands up to 2KB, You get the whole thing in on_payload()
- **Payload::Streamed**: payload arrives in pieces through on_chunk(). on_abort() lets you clean up if the transfer dies halfway

The template has a working example of each in src/commands/

Each messsage creates a new class instance, it's fine to keep per-message state in member variables

## Replies

Replies always go out on the command's own topic (its prefix and name);
a command cannot pick another one.

- ctx.reply("text")      - a fixed text reply, as it is
- ctx.reply_fmt(size, "fmt", ...) - a text reply, printf style, in a buffer of size characters (cut off past it)
- ctx.reply(ReplyBuffer) - a longer reply built in a ReplyBuffer(ctx, bytes) (pico_simple_util/reply_buffer.h);
                           it carries its topic, and if it has to wait the queue keeps a reference
                           to it (no copy, no allocation)
- ctx.begin_stream()     - larger binary data.

## MQTT topics

Commands are structured as following. id is the mqtt username

| Topic | Direction | Retained |
|---|---|---|
| `devices/<id>/status` (`online` / `offline`) | from device | yes |
| `devices/<id>/send/<cmd>[/args]` | to device | no |
| `devices/<id>/recv/<cmd>` | from device | no |
| `devices/<id>/debug/send/<cmd>` | to device | no |
| `devices/<id>/upgrade` ([OTA](ota.md)) | to device | yes |
| `devices/<id>/crash` ([crashes](logging-and-crashes.md)) | from device | — |

## Built-in commands

Look for "commands" folders for all available items.
There's a "example_commands" in one of the libraries that are available but not compiled.