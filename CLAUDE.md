# picohive-firmware

Firmware OS for Raspberry Pi Pico W / Pico 2 W (also builds for linux): OTA, crash reports, logging, web config page,
and commands over MQTT / WebSocket / Telegram / Calendar / AI. Apps are separate repositories that carry this repo
as their `os/` submodule. Overview: `README.md`, docs in `docs/`.

## Most users are here to start a project

People usually clone this repo only to create a new app. If the user wants to build something (a thermostat, a sensor,
"a new project"), or isn't sure where to start, use the **new-project** skill (`.claude/skills/new-project/SKILL.md`).
Don't write app code in this repo: apps live in their own project, created by that skill.

Skills used inside a generated project (its `CLAUDE.md` points to them through `os/.claude/skills/`):
`add-command`, `add-setting`, `ship`, `log-server`.

## Working on the OS itself

- `libraries/`: OS libraries (`pico_*`), with `linux/` and `pico/` variants and mocks in `libraries/hardware/linux/mocks/`.
- `template/`: what `create_project.sh` copies (`template/README.md`). Files in `template/files/` are real sources
  except for the tokens `@PROJECT_NAME@`, `@PROJECT_TITLE@` and `@PICO_BOARD@`; a path part `_dot_x` becomes `.x`.
  A template change has to keep building: create a scratch project with `--os-url <this checkout>` and build it.
- `web/`: the page builder (`build_web_page.py` holds the page.yaml schema) and features.
- `.devcontainer/`: container builds behind `build_via_podman.sh` / `build_via_docker.sh`.
- Same rules as apps: one thread and never block, `trace()` and never printf (the build checks), and board-only code
  behind `#if PICO_OS_PICO`.
- If a change alters how apps are written, update the matching doc in `docs/` and the skill in `.claude/skills/`.
