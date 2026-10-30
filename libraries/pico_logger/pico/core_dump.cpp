/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"

#include "flash_layout.h"
#include "pico_logger.h"
#include "core_dump.h"

static_assert(CoreDump::HEADER_AREA == FLASH_SECTOR_SIZE, "clear() erases the header's sector only");
static_assert(sizeof(CoreDump::Header) % FLASH_PAGE_SIZE == 0, "the header is programmed as whole pages");
static_assert(CoreDump::HEADER_AREA + (SRAM_END - SRAM_BASE) <= PICO_OS_SLOT_SIZE, "RAM doesn't fit in the staging slot");

uint32_t core_dump_fault_regs[8] = {};

bool CoreDump::s_saving = false;

const CoreDump::Header *CoreDump::saved()
{
    const Header *header = (const Header *)(XIP_BASE + PICO_OS_STAGING_OFFSET);

    return valid_header(*header, SRAM_BASE, SRAM_END - SRAM_BASE) ? header : nullptr;
}

uint32_t CoreDump::size()
{
    return (saved() != nullptr) ? HEADER_AREA + (SRAM_END - SRAM_BASE) : 0;
}

const uint8_t *CoreDump::data()
{
    return (const uint8_t *)(XIP_BASE + PICO_OS_STAGING_OFFSET);
}

void CoreDump::erase_header(void *)
{
    flash_range_erase(PICO_OS_STAGING_OFFSET, FLASH_SECTOR_SIZE);
}

bool CoreDump::clear()
{
    if (saved() == nullptr)
    {
        return true;
    }

    return flash_safe_execute(CoreDump::erase_header, nullptr, UINT32_MAX) == PICO_OK;
}

void CoreDump::save_fault(uint32_t exc_return, uint32_t handler_sp, uint32_t reason)
{
    uint32_t regs[NUM_REGS] = {};

    uint32_t sp = handler_sp;
    if (exc_return & (1u << 2))
    {
        __asm volatile ("mrs %0, psp" : "=r" (sp));
    }

    if ((sp >= SRAM_BASE) && (sp <= SRAM_END - 32) && ((sp & 3) == 0))
    {
        const uint32_t *frame = (const uint32_t *)sp;

        regs[0] = frame[0];
        regs[1] = frame[1];
        regs[2] = frame[2];
        regs[3] = frame[3];
        memcpy(&regs[4], core_dump_fault_regs, sizeof(core_dump_fault_regs));
        regs[12] = frame[4];
        regs[14] = frame[5];
        regs[15] = frame[6];
        regs[16] = frame[7];

        uint32_t frame_size = (exc_return & (1u << 4)) ? 32 : 104;
        regs[13] = sp + frame_size + ((frame[7] & (1u << 9)) ? 4 : 0);
    }

    save(reason, regs);
}

void CoreDump::save_here(uint32_t reason)
{
    uint32_t regs[NUM_REGS];
    core_dump_capture(regs);
    save(reason, regs);
}

void CoreDump::save(uint32_t reason, const uint32_t *regs)
{
    if (s_saving || (saved() != nullptr))
    {
        return;
    }
    s_saving = true;

    uint32_t irq = save_and_disable_interrupts();

    const uint32_t ram_size = SRAM_END - SRAM_BASE;
    char build_id[BUILD_ID_LEN] = {};
    get_build_id(build_id, BUILD_ID_LEN);

    Header header;
#if PICO_RP2040
    fill_header(header, reason, regs, 2040, SRAM_BASE, ram_size, build_id);
#else
    fill_header(header, reason, regs, 2350, SRAM_BASE, ram_size, build_id);
#endif

    const uint32_t total = HEADER_AREA + ram_size;

    for (uint32_t offset = 0; offset < total; offset += ERASE_STEP)
    {
        uint32_t len = (total - offset < ERASE_STEP) ? (total - offset) : ERASE_STEP;
        flash_range_erase(PICO_OS_STAGING_OFFSET + offset, len);
        watchdog_update();
    }

    for (uint32_t offset = 0; offset < ram_size; offset += PROGRAM_STEP)
    {
        flash_range_program(PICO_OS_STAGING_OFFSET + HEADER_AREA + offset, (const uint8_t *)(SRAM_BASE + offset), PROGRAM_STEP);
        watchdog_update();
    }

    flash_range_program(PICO_OS_STAGING_OFFSET, (const uint8_t *)&header, sizeof(header));

    restore_interrupts(irq);
}

extern "C" void core_dump_save_fault(uint32_t exc_return, uint32_t handler_sp, uint32_t reason)
{
    CoreDump::save_fault(exc_return, handler_sp, reason);
}

extern "C" void core_dump_save_here(uint32_t reason)
{
    CoreDump::save_here(reason);
}
