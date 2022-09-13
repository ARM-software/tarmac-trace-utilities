/*
 * Copyright 2016-2021 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of Tarmac Trace Utilities
 */

#ifndef LIBTARMAC_ELF_HH
#define LIBTARMAC_ELF_HH

#include <memory>
#include <string>

static constexpr unsigned EI_NIDENT = 16;

static constexpr unsigned SHT_SYMTAB = 2;
static constexpr unsigned SHT_STRTAB = 3;

static constexpr unsigned STB_LOCAL = 0;
static constexpr unsigned STB_GLOBAL = 1;
static constexpr unsigned STB_WEAK = 2;

static constexpr unsigned STT_NOTYPE = 0;
static constexpr unsigned STT_OBJECT = 1;
static constexpr unsigned STT_FUNC = 2;

/*
 * These structures don't reflect the precise ELF layout; that's dealt
 * with at load time (including normalising out endianness). As a
 * result we can make these a bit more 'cooked' than standard ELF,
 * e.g. by separating the parts of the symbol structure's st_info.
 */
struct ElfHeader {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_flags;
    uint64_t e_entry, e_phoff, e_shoff;
    uint16_t e_ehsize;
    uint16_t e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum;
    uint16_t e_shstrndx;
};

struct ElfSectionHeader {
    uint32_t sh_name, sh_type, sh_link, sh_info;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size, sh_addralign, sh_entsize;

    uint64_t entries() const;
};

struct ElfSymbol {
    uint32_t st_name;
    uint8_t st_bind, st_type; // physically, both stored in st_info
    uint8_t st_visibility;    // physically stored in st_other
    uint16_t st_shndx;
    uint64_t st_value, st_size;
};

class ElfFile {
  public:
    virtual ~ElfFile() = default;
    virtual bool is_big_endian() const = 0;
    virtual unsigned nsections() const = 0;
    virtual bool section_header(unsigned index, ElfSectionHeader &) const = 0;
    virtual bool symbol(const ElfSectionHeader &shdr, unsigned symbolindex,
                        ElfSymbol &) const = 0;
    virtual std::string strtab_string(const ElfSectionHeader &shdr,
                                      unsigned offset) const = 0;
};

std::unique_ptr<ElfFile> elf_open(const std::string &filename);

#endif // LIBTARMAC_ELF_HH
