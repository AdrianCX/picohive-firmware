# Project template

Everything needed to start a new application repository built on this one, the
way the epaper project is: a small repository of its own that carries this
repository as its `os/` submodule and gets the SDK, the libraries and the build
scripts from there.

```
create_project.sh   the script
files/              what it copies into the new project
```

## Usage

```
os/template/create_project.sh thermostat
os/template/create_project.sh --board pico_w --title "Garden watering" irrigation ~/work/irrigation
os/template/create_project.sh --board linux sensor_hub
```

The project builds for a board and for linux from the same sources
(`os/build_via_podman.sh pico2_w`, `os/build_via_podman.sh linux`); `--board`
only picks what its README and the printed next steps use.

It creates the destination (default `./<project-name>`), copies `files/` into
it, makes the project its own OTA key version 0 in `keys/` (`--no-keys` leaves
that to the first build), `git init`s it, adds this repository as the `os/`
submodule tracking the branch this checkout is on, pulls in the SDK and
picotool inside it, and commits the result.

`--os-url` takes a local path as well as a URL, which is far quicker than
cloning from GitHub; the new repository then points at that path, so change the
url in its `.gitmodules` before sharing it.

`create_project.sh --help` lists the rest.

## What the new project gets

```
CMakeLists.txt      bootstraps os/pico_os.cmake, the OTA key versions, then
                    defines the firmware
README.md
.gitignore
src/main.cpp        the get_app_info() callbacks: boot, wifi, poll loop,
                    settings; telegram and calendar ready to uncomment
src/commands/       example commands to copy: no body, a buffered body, a
                    streamed body - every .cpp here is a command
src/config/general_config.h
                    board pinout and other compiled in settings
src/web/page.yaml   web UI: wifi/MQTT config, OTA upgrade, reboot, format, and what else is on it
os/                 this repository, as a submodule
```

It builds and runs as it is: the board comes up as the `WIFI_AP_SSID` access
point while no wifi is configured, serves the page on port 80, takes its
config, accepts an OTA upgrade, and answers the example commands over mqtt and
the page's websocket.

## Editing the template

The files under `files/` are copied verbatim except for three tokens, so they
stay readable as real sources:

| token             | becomes                                          |
|-------------------|--------------------------------------------------|
| `@PROJECT_NAME@`  | the project name: CMake target, executable, elf name |
| `@PROJECT_TITLE@` | `--title`, or the name with underscores as spaces |
| `@PICO_BOARD@`    | `--board`, `pico2_w` by default, or `linux`       |

A path component named `_dot_x` is written out as `.x`, which is how
`_dot_gitignore` becomes the new project's `.gitignore` without this directory
carrying a live one.

Binary files (images, fonts) are copied through without substitution, so they
can be added to `files/` as they are.
