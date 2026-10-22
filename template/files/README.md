# @PROJECT_TITLE@

A Pico W / Pico 2 W app on [pico OS](https://github.com/AdrianCX/picohive-firmware), which lives in `os/`.
The same sources also build as a Linux program.

## Layout

```
CMakeLists.txt      the firmware: libraries, commands, web page
src/main.cpp        the callbacks the OS calls
src/commands/       one command per file
src/config/         pins and other compiled in settings
src/web/page.yaml   the web page: features on/off, your own tabs (os/web/README.md)
keys/               OTA keys (gitignored, back them up)
os/                 the OS, as a submodule
```

## Build

```
git submodule update --init --recursive
os/build_via_podman.sh @PICO_BOARD@
```

Any board builds from the same sources: `os/build_via_podman.sh pico2_w`,
`pico_w` or `linux`, each into `build/<board>/`.

On a board: flash `build/<board>/@PROJECT_NAME@.uf2` over USB the first time.
After that, upload `@PROJECT_NAME@.uf2.encrypted` from the board's page.

On linux: `os/run_via_podman.sh --net -- --trace-stdout` runs
`build/linux/@PROJECT_NAME@` at 10.89.50.10 on the "picohive" container network
(`--valgrind`, `--gdb`: see `os/docs/linux.md`); upgrades are
`@PROJECT_NAME@.encrypted`, from its page. Board only headers (`hardware/...`,
`pico/...`) go behind `#if PICO_OS_PICO`.

## Docs

Start with `os/docs/getting-started.md`, and `os/docs/linux.md` for linux.
Commands are in `os/docs/commands.md`, settings in `os/docs/project.md`, upgrades and keys in `os/docs/ota.md`.
