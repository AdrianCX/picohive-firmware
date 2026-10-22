/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#include "pico/cyw43_arch.h"

#include "app_config.h"
#include "wifi_settings.h"

static const uint32_t wifi_auth_types[WIFI_AUTH_COUNT] = {
    CYW43_AUTH_OPEN,
    CYW43_AUTH_WPA_TKIP_PSK,
    CYW43_AUTH_WPA2_AES_PSK,
    CYW43_AUTH_WPA2_MIXED_PSK,
    CYW43_AUTH_WPA3_SAE_AES_PSK,
    CYW43_AUTH_WPA3_WPA2_AES_PSK,
};

uint32_t get_wifi_auth_type()
{
    if (get_config().wifi_auth >= WIFI_AUTH_COUNT)
    {
        return CYW43_AUTH_WPA2_AES_PSK;
    }

    return wifi_auth_types[get_config().wifi_auth];
}

static const uint32_t wifi_countries[] = {
    CYW43_COUNTRY_WORLDWIDE,
    CYW43_COUNTRY_AUSTRALIA,
    CYW43_COUNTRY_AUSTRIA,
    CYW43_COUNTRY_BELGIUM,
    CYW43_COUNTRY_BRAZIL,
    CYW43_COUNTRY_CANADA,
    CYW43_COUNTRY_CHILE,
    CYW43_COUNTRY_CHINA,
    CYW43_COUNTRY_COLOMBIA,
    CYW43_COUNTRY_CZECH_REPUBLIC,
    CYW43_COUNTRY_DENMARK,
    CYW43_COUNTRY_ESTONIA,
    CYW43_COUNTRY_FINLAND,
    CYW43_COUNTRY_FRANCE,
    CYW43_COUNTRY_GERMANY,
    CYW43_COUNTRY_GREECE,
    CYW43_COUNTRY_HONG_KONG,
    CYW43_COUNTRY_HUNGARY,
    CYW43_COUNTRY_ICELAND,
    CYW43_COUNTRY_INDIA,
    CYW43_COUNTRY_ISRAEL,
    CYW43_COUNTRY_ITALY,
    CYW43_COUNTRY_JAPAN,
    CYW43_COUNTRY_KENYA,
    CYW43_COUNTRY_LATVIA,
    CYW43_COUNTRY_LIECHTENSTEIN,
    CYW43_COUNTRY_LITHUANIA,
    CYW43_COUNTRY_LUXEMBOURG,
    CYW43_COUNTRY_MALAYSIA,
    CYW43_COUNTRY_MALTA,
    CYW43_COUNTRY_MEXICO,
    CYW43_COUNTRY_NETHERLANDS,
    CYW43_COUNTRY_NEW_ZEALAND,
    CYW43_COUNTRY_NIGERIA,
    CYW43_COUNTRY_NORWAY,
    CYW43_COUNTRY_PERU,
    CYW43_COUNTRY_PHILIPPINES,
    CYW43_COUNTRY_POLAND,
    CYW43_COUNTRY_PORTUGAL,
    CYW43_COUNTRY_SINGAPORE,
    CYW43_COUNTRY_SLOVAKIA,
    CYW43_COUNTRY_SLOVENIA,
    CYW43_COUNTRY_SOUTH_AFRICA,
    CYW43_COUNTRY_SOUTH_KOREA,
    CYW43_COUNTRY_SPAIN,
    CYW43_COUNTRY_SWEDEN,
    CYW43_COUNTRY_SWITZERLAND,
    CYW43_COUNTRY_TAIWAN,
    CYW43_COUNTRY_THAILAND,
    CYW43_COUNTRY_TURKEY,
    CYW43_COUNTRY_UK,
    CYW43_COUNTRY_USA,
};

// Two letter code from the config page, anything the driver doesn't list stays on the default.
uint32_t get_wifi_country()
{
    uint32_t country = CYW43_COUNTRY(get_config().wifi_country[0], get_config().wifi_country[1], 0);

    for (uint32_t i = 0; i < sizeof(wifi_countries) / sizeof(wifi_countries[0]); ++i)
    {
        if (wifi_countries[i] == country)
        {
            return country;
        }
    }

    return CYW43_COUNTRY_NETHERLANDS;
}
