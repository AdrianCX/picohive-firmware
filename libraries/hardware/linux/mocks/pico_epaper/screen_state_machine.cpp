/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stddef.h>
#include <stdint.h>

#include "screen_state_machine.h"
#include "screen_state_machine_color.h"
#include "screen_state_machine_rb.h"

volatile uint32_t ScreenStateMachine::m_busyHighCount = 0;
volatile uint32_t ScreenStateMachine::m_busyLowCount = 0;

ScreenStateMachine *ScreenStateMachine::m_instance = NULL;

ScreenStateMachine::ScreenStateMachine()
{
    m_instance = this;
}

ScreenStateMachine *ScreenStateMachine::get()
{
    return m_instance;
}

void ScreenStateMachine::update(uint64_t now_us)
{
}

bool ScreenStateMachine::show_image(const uint8_t *image, size_t size)
{
    return true;
}

int ScreenStateMachine::onTimeout(int step, uint64_t now_us)
{
    return DONE;
}

ScreenStateMachineColor::ScreenStateMachineColor() {}
int ScreenStateMachineColor::runStep(int step, uint64_t now_us) { return DONE; }
int ScreenStateMachineColor::onTimeout(int step, uint64_t now_us) { return DONE; }

ScreenStateMachineRb::ScreenStateMachineRb() {}
int ScreenStateMachineRb::runStep(int step, uint64_t now_us) { return DONE; }
void ScreenStateMachineRb::requestStatus() {}
