/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "pico_logger.h"
#include "os_platform.h"
#include "os_linux.h"
#include "upgrade_helper.h"

#if defined(__x86_64__)
#define OTA_ELF_MACHINE EM_X86_64
#elif defined(__i386__)
#define OTA_ELF_MACHINE EM_386
#elif defined(__aarch64__)
#define OTA_ELF_MACHINE EM_AARCH64
#elif defined(__arm__)
#define OTA_ELF_MACHINE EM_ARM
#elif defined(__riscv)
#define OTA_ELF_MACHINE EM_RISCV
#else
#error "unknown CPU: add its EM_* to upgrade_helper.cpp"
#endif

#define OTA_ELF_CLASS ((sizeof(void *) == 8) ? ELFCLASS64 : ELFCLASS32)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define OTA_ELF_DATA ELFDATA2LSB
#else
#define OTA_ELF_DATA ELFDATA2MSB
#endif

static_assert(offsetof(Elf32_Ehdr, e_machine) == offsetof(Elf64_Ehdr, e_machine), "e_machine moved");
static_assert(offsetof(Elf64_Ehdr, e_machine) + sizeof(Elf64_Half) <= 20, "m_header too small");

static const char *check_elf_header(const uint8_t *header, size_t len)
{
    if ((len < 20) || (memcmp(header, ELFMAG, SELFMAG) != 0))
    {
        return "not an ELF executable, this is a linux build";
    }

    if ((header[EI_CLASS] != OTA_ELF_CLASS) || (header[EI_DATA] != OTA_ELF_DATA))
    {
        return "the executable is for another CPU (ELF class or byte order differ)";
    }

    Elf64_Half type;
    Elf64_Half machine;
    memcpy(&type, header + offsetof(Elf64_Ehdr, e_type), sizeof(type));
    memcpy(&machine, header + offsetof(Elf64_Ehdr, e_machine), sizeof(machine));

    if ((type != ET_EXEC) && (type != ET_DYN))
    {
        return "the ELF is not an executable";
    }

    if (machine != OTA_ELF_MACHINE)
    {
        trace("UpgradeHelper: e_machine=%u, this build is %u", (unsigned int)machine, (unsigned int)OTA_ELF_MACHINE);
        return "the executable is for another CPU (ELF e_machine differs)";
    }

    return nullptr;
}

static std::string staging_path()
{
    return std::string(os_data_dir()) + "/upgrade.staging";
}

bool UpgradeHelper::m_isUpgrading = false;

UpgradeHelper *UpgradeHelper::create()
{
    return new UpgradeHelper();
}

UpgradeHelper::UpgradeHelper()
{
    clear();
    m_isUpgrading = true;
}

UpgradeHelper::~UpgradeHelper()
{
    closeStaging(true);
    m_isUpgrading = false;
}

void UpgradeHelper::closeStaging(bool remove)
{
    if (m_fd >= 0)
    {
        close(m_fd);
        m_fd = -1;
    }

    if (remove)
    {
        unlink(staging_path().c_str());
    }
}

bool UpgradeHelper::clear()
{
    trace("UpgradeHelper::clear: this=%p", this);

    closeStaging(true);

    m_bytesWritten = 0;
    m_printIndex = 0;
    m_verified = false;
    m_storeFailed = false;
    m_error = nullptr;
    m_container.reset();

    m_fd = open(staging_path().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0700);
    if (m_fd < 0)
    {
        trace("UpgradeHelper::clear: this=%p cannot open %s: %s", this, staging_path().c_str(), strerror(errno));
        m_error = "cannot open the staging file";
        m_storeFailed = true;
        return false;
    }

    return true;
}

const char *UpgradeHelper::lastError() const
{
    if (m_container.lastError() != nullptr)
    {
        return m_container.lastError();
    }

    if (m_error != nullptr)
    {
        return m_error;
    }

    if (!m_storeFailed)
    {
        return "";
    }

    return "the firmware did not decrypt, it was encrypted to another key or altered";
}

bool UpgradeHelper::storeData(const u8_t *data, size_t len)
{
    if (m_storeFailed)
    {
        return false;
    }

    if (!m_container.feed(data, len, *this))
    {
        m_storeFailed = true;
        return false;
    }

    return true;
}

bool UpgradeHelper::verifySignature()
{
    if (m_storeFailed)
    {
        return false;
    }

    if (m_bytesWritten == 0)
    {
        m_error = "bad signature, binary incomplete";
        return false;
    }

    const char *elf_error = check_elf_header(m_header, (m_bytesWritten < sizeof(m_header)) ? m_bytesWritten : sizeof(m_header));
    if (elf_error != nullptr)
    {
        m_error = elf_error;
        return false;
    }

    m_verified = m_container.verify();
    return m_verified;
}

bool UpgradeHelper::onPlaintext(uint8_t *data, size_t len)
{
    if (m_fd < 0)
    {
        return false;
    }

    if (m_bytesWritten + len > MAX_BINARY_SIZE)
    {
        trace("UpgradeHelper::storeData: this=%p, more than %u bytes, too large", this, (unsigned int)MAX_BINARY_SIZE);
        m_error = "the executable is too large";
        return false;
    }

    for (size_t i = 0; (i < len) && (m_bytesWritten + i < sizeof(m_header)); ++i)
    {
        m_header[m_bytesWritten + i] = data[i];
    }

    size_t done = 0;
    while (done < len)
    {
        ssize_t written = write(m_fd, data + done, len - done);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            trace("UpgradeHelper::storeData: this=%p, write failed: %s", this, strerror(errno));
            m_error = "writing the staging file failed";
            return false;
        }
        done += written;
    }

    m_bytesWritten += len;

    if ((m_bytesWritten >> 20) != m_printIndex)
    {
        m_printIndex = (m_bytesWritten >> 20);
        trace("UpgradeHelper::storeData: this=%p bytesWritten=%d.\n", this, m_bytesWritten);
    }

    return true;
}

bool UpgradeHelper::upgrade()
{
    if (!m_verified && !verifySignature())
    {
        trace("UpgradeHelper::upgrade: this=%p refused: %s", this, lastError());
        return false;
    }

    if ((m_fd < 0) || (fsync(m_fd) != 0))
    {
        trace("UpgradeHelper::upgrade: this=%p, the staging file is gone", this);
        return false;
    }
    closeStaging(false);

    std::string target = os_exe_path();
    if (target.empty() || (rename(staging_path().c_str(), target.c_str()) != 0))
    {
        trace("UpgradeHelper::upgrade: this=%p, cannot replace %s: %s", this, target.c_str(), strerror(errno));
        return false;
    }

    // new version, no point in printing crash callstack from previous versions
    clear_saved_crash();

    trace("UpgradeHelper::upgrade: this=%p, %s replaced, restarting into it", this, target.c_str());
    force_restart(RESTART_UPGRADE);
    return false;
}
