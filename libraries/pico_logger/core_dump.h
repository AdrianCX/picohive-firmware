/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _CORE_DUMP_H
#define _CORE_DUMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void core_dump_save_fault(uint32_t exc_return, uint32_t handler_sp, uint32_t reason);
void core_dump_save_here(uint32_t reason);

extern uint32_t core_dump_fault_regs[8];
void core_dump_capture(uint32_t *regs);

#ifdef __cplusplus
}

class CoreDump
{
public:
    static const uint32_t VERSION = 2;
    static const uint32_t HEADER_AREA = 4096;
    static const uint32_t BUILD_ID_LEN = 48;
    static const uint32_t PICO_NOTE_TYPE = 0x4f434950;

    static const uint32_t NUM_REGS = 17;

    struct ElfHeader
    {
        uint8_t ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint32_t entry;
        uint32_t phoff;
        uint32_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    };

    struct ProgramHeader
    {
        uint32_t type;
        uint32_t offset;
        uint32_t vaddr;
        uint32_t paddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
        uint32_t align;
    };

    struct NoteHeader
    {
        uint32_t namesz;
        uint32_t descsz;
        uint32_t type;
        char name[8];
    };

    struct PrStatus
    {
        uint32_t si_signo;
        uint32_t si_code;
        uint32_t si_errno;
        uint16_t cursig;
        uint16_t pad;
        uint32_t sigpend;
        uint32_t sighold;
        uint32_t pid;
        uint32_t ppid;
        uint32_t pgrp;
        uint32_t sid;
        uint32_t times[8];
        uint32_t regs[NUM_REGS + 1];
        uint32_t fpvalid;
    };

    struct PicoNote
    {
        uint32_t version;
        uint32_t reason;
        uint32_t chip;
        char build_id[BUILD_ID_LEN];
    };

    struct Header
    {
        ElfHeader elf;
        ProgramHeader note;
        ProgramHeader load;
        NoteHeader core_note;
        PrStatus prstatus;
        NoteHeader pico_note;
        PicoNote pico;
        uint8_t pad[512 - 52 - 2 * 32 - 20 - 148 - 20 - 60];
    };

    static_assert(sizeof(ElfHeader) == 52 && sizeof(ProgramHeader) == 32 && sizeof(NoteHeader) == 20, "ELF layout");
    static_assert(sizeof(PrStatus) == 148 && sizeof(PicoNote) == 60, "note layout");
    static_assert(sizeof(Header) == 512, "the header is two flash pages");

    static void fill_header(Header& header, uint32_t reason, const uint32_t *regs, uint32_t chip,
                            uint32_t ram_start, uint32_t ram_size, const char *build_id);
    static bool valid_header(const Header& header, uint32_t ram_start, uint32_t ram_size);

    static const Header *saved();
    static uint32_t size();
    static const uint8_t *data();
    static bool clear();

    static void save_fault(uint32_t exc_return, uint32_t handler_sp, uint32_t reason);
    static void save_here(uint32_t reason);

private:
    static const uint32_t ERASE_STEP = 16 * 4096;
    static const uint32_t PROGRAM_STEP = 4096;

    static uint32_t signal_of(uint32_t reason);

    static void save(uint32_t reason, const uint32_t *regs);
    static void erase_header(void *unused);

    static bool s_saving;
};

#endif

#endif
