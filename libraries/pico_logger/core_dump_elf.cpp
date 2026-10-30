/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stddef.h>
#include <string.h>

#include "pico_logger.h"
#include "core_dump.h"

uint32_t CoreDump::signal_of(uint32_t reason)
{
    switch (reason)
    {
        case RESTART_FAIL: return 6;
        case RESTART_WATCHDOG: return 14;
        default: return 11;
    }
}

void CoreDump::fill_header(Header& header, uint32_t reason, const uint32_t *regs, uint32_t chip,
                           uint32_t ram_start, uint32_t ram_size, const char *build_id)
{
    memset(&header, 0, sizeof(header));

    static const uint8_t IDENT[16] = { 0x7f, 'E', 'L', 'F', 1, 1, 1 };
    memcpy(header.elf.ident, IDENT, sizeof(IDENT));
    header.elf.type = 4;
    header.elf.machine = 40;
    header.elf.version = 1;
    header.elf.phoff = offsetof(Header, note);
    header.elf.flags = 0x05000000;
    header.elf.ehsize = sizeof(ElfHeader);
    header.elf.phentsize = sizeof(ProgramHeader);
    header.elf.phnum = 2;
    header.elf.shentsize = 40;

    header.note.type = 4;
    header.note.offset = offsetof(Header, core_note);
    header.note.filesz = offsetof(Header, pad) - offsetof(Header, core_note);
    header.note.flags = 4;
    header.note.align = 4;

    header.load.type = 1;
    header.load.offset = HEADER_AREA;
    header.load.vaddr = ram_start;
    header.load.paddr = ram_start;
    header.load.filesz = ram_size;
    header.load.memsz = ram_size;
    header.load.flags = 7;
    header.load.align = 4;

    header.core_note.namesz = 5;
    header.core_note.descsz = sizeof(PrStatus);
    header.core_note.type = 1;
    memcpy(header.core_note.name, "CORE", 4);

    uint32_t signal = signal_of(reason);
    header.prstatus.si_signo = signal;
    header.prstatus.cursig = signal;
    header.prstatus.pid = 1;
    memcpy(header.prstatus.regs, regs, NUM_REGS * sizeof(uint32_t));
    header.prstatus.regs[NUM_REGS] = regs[0];

    header.pico_note.namesz = 5;
    header.pico_note.descsz = sizeof(PicoNote);
    header.pico_note.type = PICO_NOTE_TYPE;
    memcpy(header.pico_note.name, "PICO", 4);

    header.pico.version = VERSION;
    header.pico.reason = reason;
    header.pico.chip = chip;
    if (build_id != nullptr)
    {
        strncpy(header.pico.build_id, build_id, BUILD_ID_LEN - 1);
    }
}

bool CoreDump::valid_header(const Header& header, uint32_t ram_start, uint32_t ram_size)
{
    return (memcmp(header.elf.ident, "\x7f" "ELF", 4) == 0) && (header.elf.type == 4) &&
           (memcmp(header.pico_note.name, "PICO", 5) == 0) && (header.pico.version == VERSION) &&
           (header.load.offset == HEADER_AREA) && (header.load.vaddr == ram_start) && (header.load.filesz == ram_size);
}
