/* SPDX-License-Identifier: MIT

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// @PROJECT_TITLE@

#include "common_functions.h"
#include "listener.h"
#include "tls_listener.h"
#include "pico_logger.h"
#include "pico_mjson.h"
#include "session.h"

#include "app_config.h"
#include "command_request_handler.h"
#include "general_config.h"

// With pico_telegram / pico_calendar_commands linked in CMakeLists.txt:
// #include "calendar_commands.h"
// #include "telegram_commands.h"
// #include "telegram_manager.h"

#include "rc/index.html.gz.hex.h"

// Telegram buttons for /start.
// static const TelegramButton MENU[] = {
//     { "Example", "/example" },
// };

// Pins and peripherals. No network yet.
void on_boot()
{
}

bool is_access_point()
{
    return (get_config().wifi_ssid[0] == 0);
}

// Wifi failed, the board restarts after this.
void on_wifi_failed()
{
}

bool on_wifi_ready()
{
    // Outgoing TLS verifies servers: NULL uses the roots of the domains in
    // os/config/tls_domains.txt (or src/config/tls_domains.txt), or pass your own CA.
    Session::create_client_tls_config(NULL, 0);

    CommandRequestHandler::set_config(__rc_index_html_gz, __rc_index_html_gz_len);

    Listener *listener = new Listener();
    listener->listen(80, CommandRequestHandler::create);

    // Setup / reset mode, intended: plain HTTP only and no login, so a board without a config can always be reached and configured.
    if (!is_access_point())
    {
        // HTTPS needs src/certificate/ from os/util/make_cert.sh, without it only HTTP runs.
        CommandRequestHandler::require_auth(true);

        TLSListener *tls_listener = new TLSListener();
        tls_listener->listen(443, CommandRequestHandler::create);
    }

    // TelegramCommands::enable("@PROJECT_TITLE@", MENU, sizeof(MENU) / sizeof(MENU[0]));

    return true;
}

// Every loop. Never block.
void on_update(uint64_t now_us)
{
    // TelegramManager::get().update(now_us);
    // CalendarCommands::get().update(now_us);
}

// The settings, at boot and on every save. Your own: config.get_string("$.app.city", city, sizeof(city));
void on_config_update(const JsonView& config)
{
    // TelegramManager::get().load_config(config);
    // CalendarCommands::get().load_config(config);
}

void get_app_info(AppConfig *config)
{
    config->on_boot = on_boot;
    config->access_point = is_access_point;
    config->on_wifi_failed = on_wifi_failed;
    config->on_wifi_ready = on_wifi_ready;
    config->on_update = on_update;
    config->on_config_update = on_config_update;

    // ELF path, for crash call stacks.
    config->name = "build/" PICO_BOARD "/@PROJECT_NAME@";

    // Defaults to git describe of this repository.
    // config->version = "1.0";

    // Milliseconds before a stuck board restarts, 0 is off.
    config->watchdog_timeout = 60000;
}
