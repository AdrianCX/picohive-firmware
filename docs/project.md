# Project layout

[← Back](../README.md)

## Layout

```
my_app/
├── CMakeLists.txt                CMake setup
├── keys/                         OTA keys (gitignored, back them up)
├── src/
│   ├── main.cpp                  App setup, callbacks for boot, wifi, update loop.
│   ├── commands/                 User provided commands (entry points from telegram/mqtt/websockets/calendar)
│   ├── config/general_config.h   Compile-time settings
│   └── web/page.yaml             The board's web page: features on/off, its own tabs, its wording
└── os/                           This repo
```

`os/template/create_project.sh --help` shows the options.
[template/README.md](../template/README.md) has more on how the template works.

## How it runs

The OS owns `main()` and calls into your code:

```
power on
  └─ on_boot()               pins, peripherals, your settings. No network yet.
  └─ access_point()          true - start the "picow_config" access point
  └─ join Wi-Fi
       ├─ failed - on_wifi_failed() - restart
       └─ ok     - on_wifi_ready()  - start listeners
  └─ loop forever:
       on_update(now_us)
       network, MQTT, watchdog, logs
```

These are defined in get_app_info() at the bottom of main.cpp.

Notes:
1. on_config_update() hands you the settings, once at boot and again whenever the page saves them.
2. Returning false from on_wifi_ready() restarts the board.
3. get_app_info() fields:
3.1. name is the path to your ELF to help with crash reports command.
3.2. version is a user defined string or if missing the git version
3.3. watchdog_timeout is how many milliseconds of silence before the board restarts itself. 0 turns it off.

## How to use things

There's one thread, this calls periodically the update(), everything shares the same loop.

If you sleep or spin waiting for something it may break other things.
It's easier to keep an internal state, check now_us and wait for next call.

Use trace() for logging. printf isn't linked in (smaller code size, less worries about potential hangs and stack usage), build fails if something drags it in.

## Settings file

```json
{
  "wifi": {
    "ssid": "home",
    "pass": "********",
    "auth": 2,
    "country": "NL"
  },
  "web": {
    "user": "admin",
    "pass": "********"
  },
  "mqtt": {
    "host": "broker.lan",
    "user": "garden",
    "pass": "********",
    "tls": false,
    "port": 1883
  },
  "log": {
    "port": 23500,
    "key": "********"
  },
  "app": {
    "city": "Amsterdam"
  }
}
```

To add new settings, give them a section on the Config tab in src/web/page.yaml:

```yaml
config:
  add_section:
    app:
      label: My app
      fields:
        - { config: app.city, label: "City:" }
```

Then read it in on_config_update() in main.cpp:

```cpp
void on_config_update(const JsonView& config)
{
    config.get_string("$.app.city", city, sizeof(city));
}
```

Encryption:
- All settings are saved as one encrypted JSON document.
- The document is kept in config.enc. It is encrypted with a key derived from a board unique ID.
- A flash dump doesn't show your passwords, and the file won't open on another board.
- This is a minor inconvenience to someone determined, it's still easy to determine the key.
- If you need more, consider using RP2350 with OTP/secure boot.

How it works:
- Web page sends a JSON, board stores that encrypted.
- On boot and every change - everyone gets notified with a jsonview of the whole file
- If the file is missing or corrupted - board starts unconfigured / ideally in access point mode.

Sensitive fields:
- Fields named pass, key or token, or starting with secret are treated as secrets.
- These are not sent back to the webpage, only an indication that they are set or not.
- On the page a secret field shows "saved" with a Forget button: empty keeps the saved one, Forget clears it (`type: secret` in page.yaml).

## The web page

- src/web/page.yaml says what is on it: which built-in features, the board's own Config sections and tabs, and its wording. See [web/README.md](../web/README.md).
- The build makes one file with everything inline, then minifies and gzips it into flash.
- It talks to the board over a WebSocket to avoid multiple connections.

## Before putting it somewhere real

- Change the access point name and password in os/config/wifi.h
- Back up keys/
- Turn on the watchdog.
- Set a log key if the logs leave your network
- Make your own HTTPS certificate with os/util/make_cert.sh, without one only HTTP runs
- Set a page login (web.user / web.pass), the page is open to the LAN without one
