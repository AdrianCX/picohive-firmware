/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <inttypes.h>
#include "pico/cyw43_arch.h"
#include "pico/sync.h"
#include "pico_sleep.h"

// Library is RP2350 specific for now
#ifndef PICO_RP2040
#include "hardware/gpio.h"
#include "hardware/powman.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/usb.h"
#include "hardware/watchdog.h"

#include "pico_logger.h"

uint32_t __uninitialized_ram(clear_sleep_reason);
#define RESET_SLEEP_REASON 0xdeadbe3f


bool pico_deepsleep_init(uint64_t sleep_time_ms)
{
    clear_sleep_reason = 0;
    
    cyw43_arch_lwip_end();
    cyw43_arch_deinit();

    set_sys_clock_48mhz();

    // pico logger
    #define ALARM_NUM 0
    hw_clear_bits(&timer_hw->inte, 1u << ALARM_NUM);
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // pico logger watchdog restart
    watchdog_disable();
   
    // Unlock the VREG control interface
    hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);

    // Turn off USB PHY and apply pull downs on DP & DM
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS | USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
    
    usb_hw->phy_direct_override = USB_USBPHY_DIRECT_RX_DM_BITS | USB_USBPHY_DIRECT_RX_DP_BITS |          USB_USBPHY_DIRECT_RX_DD_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_FSSLEW_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_HISEL_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_HISEL_OVERRIDE_EN_BITS;

    // The powman timer keeps running across deep sleep, so only set it up after a power-on reset.
    if (!powman_timer_is_running())
    {
        powman_timer_set_1khz_tick_source_lposc();
        powman_timer_set_ms(0);
        powman_timer_start();
    }

    // Allow power down when debugger connected
    powman_set_debug_power_request_ignored(true);

    uint64_t now_ms = powman_timer_get_ms();
    uint64_t abs_time_wake_ms = now_ms + sleep_time_ms;

    powman_enable_alarm_wakeup_at_ms(abs_time_wake_ms);

    return true;
}

bool pico_deepsleep_execute()
{    
    powman_power_state P1_7 = POWMAN_POWER_STATE_NONE;

    // Wake with everything on, like a power-on
    powman_power_state P0_0 = POWMAN_POWER_STATE_NONE;
    P0_0 = powman_power_state_with_domain_on(P0_0, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
    P0_0 = powman_power_state_with_domain_on(P0_0, POWMAN_POWER_DOMAIN_XIP_CACHE);
    P0_0 = powman_power_state_with_domain_on(P0_0, POWMAN_POWER_DOMAIN_SRAM_BANK0);
    P0_0 = powman_power_state_with_domain_on(P0_0, POWMAN_POWER_DOMAIN_SRAM_BANK1);

    powman_power_state off_state = P1_7;
    powman_power_state on_state = P0_0;

    bool valid_state = powman_configure_wakeup_state(off_state, on_state);
    if (!valid_state) {
        //trace("sleep: powman_configure_wakeup_state failed, invalid state");
        return false;
    }

    // reboot to main
    powman_hw->boot[0] = 0;
    powman_hw->boot[1] = 0;
    powman_hw->boot[2] = 0;
    powman_hw->boot[3] = 0;

    // Switch to required power state
    int rc = powman_set_power_state(off_state);
    if (rc != PICO_OK) {
        //trace("sleep: powman_set_power_state failed, rc: %d", rc);
        return false;
    }

    set_restart_reason(RESTART_SLEEP);

    for (int i=0;i<10000;++i)
    {
        __wfi();
    }

    return false;
}

bool pico_deepsleep(uint64_t sleep_time_ms)
{
    if (!pico_deepsleep_init(sleep_time_ms))
    {
        return false;
    }
    
    return pico_deepsleep_execute();
}

void pico_deepsleep_set_gpio(int hw_instance, uint8_t gpio, bool edge, bool high)
{
    powman_enable_gpio_wakeup(hw_instance, gpio, edge, high);
}

bool pico_deepsleep_wake_reason_gpio(uint8_t hw_instance)
{
    if (clear_sleep_reason == RESET_SLEEP_REASON)
    {
        return false;
    }
    // pico-sdk - src/rp2350/hardware_regs/include/hardware/regs/powman.h

    // Register    : POWMAN_LAST_SWCORE_PWRUP
    // Description : Indicates which pwrup source triggered the last switched-core
    //               power up
    //               0 = chip reset, for the source of the last reset see
    //               POWMAN_CHIP_RESET
    //               1 = pwrup0
    //               2 = pwrup1
    //               3 = pwrup2
    //               4 = pwrup3
    //               5 = coresight_pwrup
    //               6 = alarm_pwrup
    return ((powman_hw->last_swcore_pwrup & (2 << hw_instance)) != 0);
}

uint8_t pico_deepsleep_wake_reason()
{
    if (clear_sleep_reason == RESET_SLEEP_REASON)
    {
        return 0;
    }
    
    return powman_hw->last_swcore_pwrup & 0x7f;
}

void pico_deepsleep_clear_reason()
{
    clear_sleep_reason = RESET_SLEEP_REASON;
}

#else
bool pico_deepsleep_init(uint64_t sleep_time_ms) { return false; }
void pico_deepsleep_set_gpio(int hw_instance, uint8_t gpio, bool edge, bool high) {}
bool pico_deepsleep_execute() { return false; }
bool pico_deepsleep_wake_reason_gpio(uint8_t hw_instance) { return false; }
bool pico_deepsleep(uint64_t sleep_time_ms) { return false; }
#endif
