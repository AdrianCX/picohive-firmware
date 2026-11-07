---
name: ship
description: Build a picohive firmware project and deliver it to a device over the air (PicoHive upload + deploy, or the board's web page / MQTT), then confirm the device runs the new build. Use when the user asks to ship, deploy, flash OTA, push or update firmware on a device.
---

# Ship a build over the air

OTA background: `os/docs/ota.md`. A device accepts only firmware signed with the key version it runs. Read
`PICO_OS_KEY_VERSION` / `PICO_OS_NEXT_KEY_VERSION` in `CMakeLists.txt`, and never change them or `keys/` as part of a ship.

## 1. Build

```bash
os/build_via_podman.sh <board>     # or os/build_via_docker.sh
```

The board is the one the target device runs (`pico_w`, `pico2_w`; on PicoHive, `get_device` shows the device type/build).
Check that `build/<board>/<name>.uf2.encrypted` and `build/<board>/<name>.elf` exist and are newer than the change.
Mention uncommitted changes to the user: the build id comes from `git describe`, so committing first makes crash reports traceable.

## 2. Deliver

Ask the user before uploading or deploying. Name the device and the build.

### With PicoHive (the picohive MCP tools are available)

1. `upload_firmware` with the device: it returns a one-time URL (valid 10 minutes). Upload both files with curl the way the
   tool's reply describes (the `.uf2.encrypted` and the `.elf`, so crashes get symbolized).
2. `get_device` to find the new slot, then `deploy_firmware` with that slot.
3. If the device has no source registered (`get_device_source`), offer `set_device_source` (repo = project path,
   build = the command above).

### Without PicoHive

- Board's page: open `http://<board-ip>`, Firmware tab, upload `build/<board>/<name>.uf2.encrypted`. This step is the user's.
- MQTT, if the user gives the broker details:
  `mosquitto_pub -h <broker> -u <user> -P <pass> -r -t devices/<id>/upgrade -f build/<board>/<name>.uf2.encrypted`

## 3. Confirm

With PicoHive, check `fleet_status` / `get_device` until the device is back online on the new build id, and look at
`read_log` for the boot. If it crashes or doesn't come back, use `list_crashes` / `get_crash` and debug from the source.
Report times to the user in local time.

Without PicoHive, ask the user to check the page's Firmware tab, or read the logs with the `log-server` skill (`os/.claude/skills/log-server/SKILL.md`).
