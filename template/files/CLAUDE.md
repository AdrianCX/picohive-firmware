# @PROJECT_TITLE@

Firmware for a Raspberry Pi Pico W / Pico 2 W (also builds as a Linux program),
built on picohive-firmware, which is the `os/` submodule. Project name / CMake
target / ELF: `@PROJECT_NAME@`. Default board: `@PICO_BOARD@`.

## Layout

- `src/main.cpp`: the callbacks the OS calls (`get_app_info()` at the bottom). The OS owns `main()`.
- `src/commands/`: one command per `.cpp`. Every `.cpp` here is built and registered automatically.
- `src/config/general_config.h`: compile-time settings (pins, sizes).
- `src/web/page.yaml`: the board's web page (features on/off, Config sections, tabs, buttons).
- `CMakeLists.txt`: libraries (`APP_LIBRARIES`), command sets (`pico_commands_add`), OTA key versions.
- `keys/`: OTA signing keys. Gitignored. Never delete or regenerate them: devices only accept firmware signed with the key version they run.
- `os/`: the SDK, libraries and build scripts. Don't edit it from this project; changes to it belong upstream.

## Build and run

```bash
os/build_via_podman.sh @PICO_BOARD@      # or build_via_docker.sh; any of pico_w, pico2_w, linux
```

Outputs are in `build/<board>/`: `@PROJECT_NAME@.uf2` (flash over USB), `@PROJECT_NAME@.uf2.encrypted` (OTA),
`@PROJECT_NAME@.elf` (symbols for crash reports). On linux the outputs are `build/linux/@PROJECT_NAME@` and `@PROJECT_NAME@.encrypted`,
and `os/run_via_podman.sh --net -- --trace-stdout` runs it (`--valgrind`, `--gdb`: `os/docs/linux.md`).

Always build after a change. The build is also the check: it fails if stdio/printf gets linked in, and
it reports stack usage.

## Rules for code here

- **One thread, never block.** Everything runs from one loop that calls `on_update(now_us)`. No sleeps and no busy waits:
  keep state in members and check `now_us` on the next call. Blocking breaks networking and trips the watchdog.
- **Log with `trace()`**, never `printf`/`std::cout` (they aren't linked, and the build fails).
- **Don't crash before the network is up**, because then OTA can't fix it and the board needs USB.
- Board-only headers (`hardware/...`, `pico/...`) go behind `#if PICO_OS_PICO` so the linux build keeps working.
- Settings that change at runtime come from the web page (`on_config_update`). Compile-time ones go in `src/config/general_config.h`.

## Common tasks

Each of these has a step-by-step guide in `os/.claude/skills/`. Follow it when doing the task:

- New command (MQTT / WebSocket / Telegram / Calendar / AI tool): `os/.claude/skills/add-command/SKILL.md`
- New setting on the Config page: `os/.claude/skills/add-setting/SKILL.md`
- Build and ship over the air to a device: `os/.claude/skills/ship/SKILL.md`
- Receive and read a device's logs: `os/.claude/skills/log-server/SKILL.md`

## Docs

`os/docs/`: `getting-started.md`, `project.md` (callbacks, settings), `commands.md`, `ota.md` (keys, rotation),
`logging-and-crashes.md`, `linux.md`. The page.yaml schema is documented in `os/web/build_web_page.py`, and
`src/web/page.yaml` has commented examples.
