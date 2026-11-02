---
name: add-command
description: Add a command to a picohive firmware project (src/commands/), reachable over MQTT, the web page's WebSocket, Telegram, Calendar or as an AI tool. Use when the user wants the board to respond to a message, button, topic or bot command, or asks to add/write a command.
---

# Add a command

Reference: `os/docs/commands.md`. Working examples, one per payload kind, are in the project's
`src/commands/` (from the template, `os/template/files/src/commands/` if they were deleted):

| payload                     | example              | handler(s) |
|-----------------------------|----------------------|------------|
| none (args are in the topic) | `example.h/.cpp`     | `on_header(ctx)`; `ctx.args` is the text after `<name>/` |
| `Payload::Buffered`, up to `max_bytes` (2KB max) | `example_buffered.*` | `on_payload(ctx, body)` |
| `Payload::Streamed`, any size | `example_streamed.*` | `on_header`, `on_chunk(ctx, data, len, last)`, `on_abort(ctx, reason)` |

Pick the smallest kind that fits. Copy the matching example's shape instead of writing it from memory.

## Steps

1. Create `src/commands/<name>.h` and `.cpp` with a `struct` that has:
   - `static constexpr std::string_view name = "<name>";` (lowercase, `-` allowed, unique; check `/help` and other command folders)
   - `static constexpr std::string_view help = "<name>[/<args>] - what it does";` (shown in `help` and to AI tools, so make it useful)
   - payload settings and handlers from the table above.
   - For streamed commands sent as retained MQTT messages, add `clears_retained = true` (see the streamed example).
2. Register it at the bottom of the `.cpp`, one macro per channel the user wants:
   `MQTT_COMMAND(X);`, `WS_COMMAND(X);`, `TELEGRAM_COMMAND(X);` (needs `pico_telegram`), `CALENDAR_COMMAND(X);`
   (needs `pico_calendar_commands`; an event titled `<device id>/<name>/args` runs it), `AI_COMMAND(X);` (an OpenAI tool:
   no Streamed payload; optional `ai_help` of 64 characters at most). Details: `os/libraries/pico_commands/command_registration.h`.
   Telegram/calendar/openai also need their library in `APP_LIBRARIES`, their command set in `pico_commands_add`, and the
   feature turned on in `page.yaml`. `src/main.cpp` has the hooks, commented out.
3. Reply with `ctx.reply("...")`, `ctx.reply_fmt(size, "fmt", ...)`, a `ReplyBuffer` for long text, or
   `ctx.begin_stream()` for binary. Replies go back on the command's own topic. Return `false` only to drop the connection.
4. Keep handlers short and non-blocking. Long work becomes state that `on_update()` advances. Per-message state can live in
   members: each message gets a fresh instance. An instance must fit `MAX_COMMAND_INSTANCE_BYTES` (256), so keep big buffers
   out of the struct (use a `ReplyBuffer`/`SharedBuffer` while it runs). The build fails with a static_assert if it doesn't fit.
5. If the user wants a button for it, add one to `src/web/page.yaml` under `tabs: add_tab: <tab>: buttons:`
   (`- Label: <name>/args`; also `confirm:` and `danger: true`).
6. Build (`os/build_via_podman.sh <board>`) and fix any errors.
7. Tell the user how to call it: MQTT `devices/<id>/send/<name>[/args]` (reply on `devices/<id>/recv/<name>`),
   WebSocket `/rc/<name>/args`, or `send_command` with the picohive MCP after it's deployed (`ship` skill).

## Using the library code

Before writing helpers, look at what `os/libraries/` already offers (HTTP client, JSON (`pico_mjson`), filesystem (`pico_fs`),
reply buffers (`pico_simple_util`), calendar, telegram, ...) and use it. `os/libraries/pico_commands/commands/` and
`example_commands/` show more complete real commands (upload, download, file streaming).
