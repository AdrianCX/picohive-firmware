# Simple Pico Web Firmware

- Batteries included firmware for PicoW, makes projects easy to set up and fun
- Paired with a quick Web management UI to help things out: https://picohive.com

## Features

- **Encrypted OTA**: Updates are sent encrypted and signed with board metadata. Board refuses if any check fails.
- **Crash reports**: saved across reboots and reported with full call stack
- **Logging**: centralized, sent encrypted over UDP
- **Setup**: just invoke the template script to create a project
- **Extending**: Put classes in commands folder, use a macro to allow calls from: MQTT, WebSocket, Telegram, Calendar, AI tools
- **Configuring**: Access point on first boot with a themed config page, settings are kept in flash encrypted
- **Protocols**: HTTP, HTTPS, WebSocket, MQTT, openAI, google calendar, telegram - all funneled into commands
- **Building**: call one script, builds easily in a container, no toolchain needed
- **Memory**: Simple efficient code, script for stack size on build, runtime prints for monitoring
- **Linux**: same apps running on linux under valgrind/gdb, mocked hardware

## Docs

- [Getting started](docs/getting-started.md): quick setup page
- [Project layout](docs/project.md): how projects are structured
- [Commands](docs/commands.md): writing commands, MQTT topics
- [Web page](web/README.md): what goes on the page, its tabs, text and skins
- [OTA upgrades](docs/ota.md): signing, encryption, keys
- [Logs and crashes](docs/logging-and-crashes.md): logs/call stack info
- [Linux](docs/linux.md): building and running apps on a PC

## Quick start

```bash
git clone --recursive https://github.com/AdrianCX/picohive-firmware.git os
os/template/create_project.sh --board pico2_w thermostat ~/work/thermostat

cd ~/work/thermostat
os/build_via_podman.sh pico2_w
```

- Flash `build/pico2_w/thermostat.uf2` over USB
- Join the `picow_config` Wi-Fi, open http://192.168.4.1 and configure

The rest is in [Getting started](docs/getting-started.md)

## Boards

Any pico-sdk board with cyw43 should compile work. Pico1/2 W tested the most

## Security

Config file:
- Stored encrypted with a key each board derives from its own unique ID
- A flash dump won't reveal secrets, the file won't open on another board
- Still, this is obfuscation, key can be recovered with knowledge of how the key is made
- Encryption is added to provide hooks for later RP2350 OTP / secure boot feature

Firmware updates:
- Signed with developer key, keep signing private key hidden
- Encrypted to hide file in transit, pico can still decrypt
- Rotating keys is easy: [OTA upgrades](docs/ota.md)
- Build folder provides regular .uf2 for usb flashing and .uf2.encrypted for OTA

Outgoing TLS:
- Every connection checks the server's certificate chain and host name
- The trusted roots are built into the firmware via [config/tls_domains.txt](config/tls_domains.txt)\
  build connects to all hosts finds root cert within build machine's CA bundle
- Can be overridden in app via src/config/tls_domains.txt, `root <name>` adds a root forcefully
- The network is only used when the list changes for convenience

OpenAI key:
- The board calls OpenAI directly, with no server in between, so the API key has to be stored on the board
- Config file is encrypted with a key derived from the board.
- Anyone with the board in hand can recover it, see config file above, planning OTP solution in the future
- Use an OpenAI project key with a spending cap, and monitor its usage. Best practice for any API key

Calendar commands:
- Always use a dedicated calendar with invitations off, it's easy to set up and avoids running commands that other people send as event invites

## Design decisions

- No safe mode, test new builds on a board on your desk first. Do not crash before the network comes up
- Yes power outage during OTA will cause problems, you can use partitions on 2350 (not on 2040) or you can fix it with a USB cable if it happens, this should be fixed in the future, I haven't seen this issue so far
- Alternative, just pust a battery pack with a diode next to your project so it kicks in when power goes out. (and measure it so you can detect it)
- Setup (access point) mode serves the page on plain HTTP with no login, on purpose, so a reset board can always be configured
- On your Wi-Fi the page is on HTTP and HTTPS with an optional login, without a login anything on the LAN can use the page and its commands
- Idle connections count towards a limit of 5
- The WebSocket implementation is minimal, sufficient for use case but don't expect full protocol parity

## Discussions

There's a discussion board on picohive.com, feel free to use that

## Contributing

Up to author, discuss first, in general:
- New libraries are welcome if they're useful and come with an example project that uses them
- Bug fixes are welcome too, with the fix included and description of project where you had the issue
- Large PRs, likely not 
- Testing/CI frameworks, keep them in separate repos and use this as submodule

## License

MPL-2.0, see [LICENSE](LICENSE). Changes to the SDK's files stay open; your own app files can use any license

Example code is MIT: everything in `template/` and `libraries/pico_commands/example_commands/`

Third-party code in submodules/ and some library folders keeps its own license
