# Getting started

[← Back](../README.md)

Items needed:  Pico W or Pico 2 W, USB cable, Linux/macOS/WSL on Windows, git, podman or docker

## 1. Create a project

```bash
./template/create_project.sh --board pico2_w --title "Thermostat" thermostat ~/work/thermostat
```

## 2. Building


```bash
cd ~/work/thermostat
./os/build_via_podman.sh pico2_w        # or ./os/build_via_docker.sh, replace pico2_w with whatever board you have
```

Results are in `build/pico2_w/` (or whatever board name you used)
- Folder: build/pico2_w/  (pico2_w is the second parameter to the script)
- thermostat.uf2           - flash over USB
- thermostat.uf2.encrypted - OTA signed/encrypted binary
- thermostat.elf           - Build with debug symbols, used for crash reports

## 3. Flash

Hold BOOTSEL, plug the board in, and copy thermostat.uf2 to the drive that shows up

## 4. Wi-Fi

With nothing configured, the board starts its own access point:
- Wifi: picow_config
- Password: randompassword
- Captive portal should open automatically, otherwise: http://192.168.4.1
- Configure save and restart
- Setup mode is plain HTTP with no login on purpose. Once on your Wi-Fi the page is on both http:// and https:// and asks for the page login, if one is set
- To reset: push flash filesystem in the web page or copy universal-flash-nuke.uf2

TODO: picohive config


## 5. Reading logs

- Logs go over UDP to the MQTT host address, on the configured logging port (default 23500)
- On the remote end you can either use a management page or you can use the script below:

```bash
python3 os/util/log_decrypt.py --port 23500
```

More info: [Logs and crashes](logging-and-crashes.md)

## 6. Update over Wi-Fi

- Web page, open the board's address and upload thermostat.uf2.encrypted
- MQTT via management page
- publish the file as a retained message to the board's upgrade topic:

```bash
mosquitto_pub -h <broker> -u <user> -P <pass> -r -t devices/<device-id>/upgrade -f build/pico2_w/thermostat.uf2.encrypted
```

Firmware checks if uf2 is compatible and decrypts/verifies signature before applying it

More info: [OTA upgrades](ota.md)

## 7. Customize

Typical starting points:
- src/main.cpp      - board init, update function and general app config
- src/web/page.yaml - web page: what is on it, see os/web/README.md
- src/commands/     - drop classes you want to be executed when you receive commands via mqtt/telegram/websocket/calendar/... (need to register via macro which medium)

Commands info: [Commands](commands.md)

Next: [Your project](project.md)
