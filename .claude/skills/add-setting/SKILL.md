---
name: add-setting
description: Add a runtime setting to a picohive firmware project, shown on the board's Config page (src/web/page.yaml) and read in on_config_update() in src/main.cpp. Use when the user wants something configurable from the web page (a city, interval, threshold, API key) instead of compiled in.
---

# Add a setting

Settings are one encrypted JSON document, edited on the board's Config page and handed to the app via
`on_config_update(const JsonView&)` at boot and on every save. Background: `os/docs/project.md`, "Settings file".

Compile-time constants (pins, buffer sizes) don't belong here; they go in `src/config/general_config.h`.

## Steps

1. **Page:** in `src/web/page.yaml`, under `config: add_section:`, add the field to the app's section (create one if needed,
   usually keyed `app`):

   ```yaml
   config:
     add_section:
       app:
         label: My app
         fields:
           - { config: app.city, label: "City:" }
           - { config: app.interval, label: "Every (min):", type: number, default: 10 }
           - { config: app.mode, label: "Mode:", type: select, choices: [auto, manual], default: auto }
           - { config: app.enabled, label: "Enabled:", type: checkbox }
           - { config: app.secret_api, label: "API key:", type: secret }
   ```

   Types: `text` (default), `number`, `checkbox`, `secret`, `select` (`choices` is a list or a value→label map).
   Also available: `default`, `placeholder`. The schema lives in `os/web/build_web_page.py` (`field_html`); the build
   reports unknown keys with suggestions.

   **Secrets:** a field whose last path part is exactly `pass`, `key` or `token`, or starts with `secret`, is never sent back
   to the page (`ConfigStore::is_secret`). `api_key` does **not** match: use `key`, or a name like `secret_api`, plus `type: secret`.

2. **Code:** store the value in the class or file that uses it, and read it in `on_config_update()` in `src/main.cpp` (or in
   the owning class's `load_config(config)`, the way `TelegramManager` does):

   ```cpp
   config.get_string("$.app.city", m_city, sizeof(m_city));   // char buffer, sized for the longest valid value
   config.get_uint("$.app.interval", m_interval_min);
   config.get_bool("$.app.enabled", m_enabled);
   ```

   Other getters: `get_int`, `get_number`, `get_whole_string` (`os/libraries/pico_mjson/pico_mjson.h`). Each returns
   false when the value is missing, so set defaults first. This runs again on every save: apply the new value, and don't
   allocate on every call.

3. Build (`os/build_via_podman.sh <board>`) and fix any errors, including page.yaml errors from the web build step.
4. Tell the user where the field appears (Config tab, under the section label) and that saving applies it right away, with no reboot.
