# Web page

[← Back](../README.md)

Each app describes its page in `src/web/page.yaml`. The build turns that into one gzipped HTML file in flash.

```yaml
name: "Thermostat"           # page title

config:                      # the Config tab (General and Server are always there)
  telegram: on               # built-in sections: telegram, calendar, openai, mcp
  add_section:               # your own settings, read in on_config_update()
    app:
      label: Heating
      fields:
        - { config: app.target, label: "Target (C):", type: number, default: 21 }
        - { config: app.mode, label: "Mode:", type: select, choices: [auto, manual] }
        - { config: app.secret_api, label: "API key:", type: secret }

tabs:                        # tabs after Config
  firmware: on               # built-in, on by default
  add_tab:
    control:
      label: Control
      buttons:
        - Heat on: heat/on                                   # label: command
        - { label: Reset, command: reboot, confirm: "Sure?", danger: true }
  # order: [control, config]  # optional, the first one is where the page opens
```

## Fields

`config:` is the setting's path in the settings JSON. Every field needs it.

| type       | notes |
|------------|-------|
| `text`     | default |
| `number`   | |
| `checkbox` | `default: true` to start checked |
| `select`   | `choices:` a list, or a map of value: label |
| `secret`   | never sent back to the page. The name must be `pass`, `key`, `token` or start with `secret` |

Also `label:`, `default:` and `placeholder:`.

## Built-ins

- Built-in features live in `features/`. Each one also needs its library linked in `CMakeLists.txt`; the build warns if it isn't.
- `firmware` is on by default. The others are off until you turn them on.
- Options go in place of `on`, e.g. `calendar: { id_example: garden }`.

## More than fields and buttons

- A section or tab can have its own `html:`, `script:` and `style:` files.
- Or it can come from a folder: `add_tab: { arm: ./arm }`. Use the same layout as `features/`: `page.html` for a tab or
  `section.html` for a Config section, plus `*.js`, `*.css`, `strings.yaml`.

## Wording and skins

- The page has three skins: `cartoon`, `clean`, `xeno`.
- To change any text, point `text:` at a yaml file of `key: { clean: ..., cartoon: ... }`, the same format as `strings.yaml` in `base/` and `features/`.

## Checking it

Mistakes fail the build with a "did you mean" hint. To see what went into the page and where each part came from:

```bash
os/web/build_web_page.py src/web/page.yaml -o /tmp/page.html --explain
```
