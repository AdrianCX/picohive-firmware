---
name: new-project
description: Create a new picohive firmware project (Pico W / Pico 2 W / linux) from this repo's template, build it, and get it onto a board, either guiding the user step by step or doing it automatically. Use when the user asks to create, start, scaffold or set up a new pico / picohive project, app or firmware.
---

# New picohive project

The template script does the scaffolding: `template/create_project.sh` (relative to this repo's root, the
directory holding this `.claude/`). It never prompts, so you can run it directly. `template/README.md`
explains what it creates.

## 1. Gather the inputs

You need four things. Take what the user already said, infer the rest, and ask only for what is really
missing, in a single question:

| input       | rule / default |
|-------------|----------------|
| name        | `^[A-Za-z][A-Za-z0-9_]*$` (it becomes a CMake target and a C string). Turn "garden watering" into `garden_watering` |
| title       | human-readable, default is the name with spaces |
| board       | `pico2_w` (default), `pico_w` or `linux`. The project builds for all of them; this only picks the README and next steps |
| destination | default `../<name>` next to this checkout; never inside a git repository (the script refuses) |

In the same question, ask about the **mode**:
- **Do it for me** (automatic): you run every step a computer can do and only stop for physical steps.
- **Guide me**: you explain each step, the user runs it, and you check the result before moving on.

If the user says "just do it" or similar, use automatic mode and the defaults.

## 2. Create it

```bash
<os>/template/create_project.sh --board <board> --title "<title>" --os-url <os> <name> <dest>
```

- Pass `--os-url <absolute path of this checkout>`. Cloning locally is much faster than GitHub.
- The recursive SDK/picotool fetch takes minutes. Run the command in the background and wait for it.
- If it says "Nothing committed", git has no user.name/email. Tell the user; don't set it yourself.

Then point the submodule at the public URL, so the new repo also works for someone else cloning it.
`--os-url` left a local path in `.gitmodules`:

```bash
url="$(git -C <os> remote get-url origin 2>/dev/null || echo https://github.com/AdrianCX/picohive-firmware.git)"
cd <dest>
git config -f .gitmodules submodule.os.url "$url"
git submodule sync -q
git add .gitmodules
git commit -q --amend --no-edit     # only if the initial commit exists; otherwise leave it staged
```

## 3. Build

```bash
cd <dest> && os/build_via_podman.sh <board>     # or os/build_via_docker.sh
```

Use whichever of `podman` / `docker` is installed (`command -v`). If neither is installed, stop and tell the user. The first
build creates the container image and takes a while: run it in the background. Check that
`build/<board>/<name>.uf2` (or `build/linux/<name>` for linux) exists.

## 4. Tell the user about keys

`<dest>/keys/` holds the OTA signing keys. It is gitignored and is **the only copy**: if it's lost, devices accept firmware
only over USB. Tell the user to back it up. Never delete or regenerate it.

## 5. Get it running

### Pico board

These steps need hands on the board. In both modes, walk the user through them one at a time:

1. Hold BOOTSEL, plug the board in, and copy `build/<board>/<name>.uf2` to the drive that appears.
   In automatic mode, offer to do the copy: look for an `RPI-RP2`/`RP2350` mount under `/run/media/$USER` or `/media/$USER`.
2. Join the `picow_config` Wi-Fi (password in `os/config/wifi.h`, default `randompassword`) and open http://192.168.4.1.
3. Fill in Wi-Fi on the Config tab, and pick the Server mode: local only, full MQTT managed, or local with logs sent to a log server, then save. The board restarts onto the user's Wi-Fi.

**PicoHive** (only if the picohive MCP tools are available, and only after the user agrees):
- `add_device` with the project name (and a type if the user wants one). The secrets aren't returned: the user
  opens the device on the PicoHive dashboard, uses Device Config → Export, and pastes or scans that config into the board's page.
  That covers MQTT, logs and OTA in one go.
- `set_device_source` with repo = `<dest>`, build = `os/build_via_podman.sh <board>`, so crash debugging finds the source later.
- After the board reboots, check `fleet_status` / `get_device` until it shows online, and report its build id.

Without PicoHive, point the user to `os/docs/getting-started.md` sections 5–6 (logs with
`os/tools/log_server.py`, OTA from the board's page or MQTT).

### Linux

```bash
os/run_via_podman.sh --net -- --trace-stdout     # at 10.89.50.10 on the "picohive" network
```

In automatic mode, run it in the background and check that the trace output shows it serving on port 80. It starts
unconfigured, like a board in access-point mode; see `os/docs/linux.md`.

## 6. Hand-off

Finish with a short summary: the path, the build outputs, the keys reminder, and what to edit next
(`src/main.cpp`, `src/commands/`, `src/web/page.yaml`). Tell the user to continue there with `cd <dest> && claude`;
the checkout this ran from isn't needed for that project anymore. Offer the follow-up skills by name:
`add-command`, `add-setting`, `ship`. The new project's `CLAUDE.md` points future sessions to them.
