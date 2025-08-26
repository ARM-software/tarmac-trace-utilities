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

/*
 * Minimal ELF-reading system for extracting just the things needed
 * for this project.
 */

#include "libtarmac/elf.hh"
#include "libtarmac/misc.hh"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <sstream>

using std::ostringstream;
using std::string;

static constexpr unsigned EI_MAG0 = 0;
static constexpr unsigned EI_MAG1 = 1;
static constexpr unsigned EI_MAG2 = 2;
static constexpr unsigned EI_MAG3 = 3;
static constexpr unsigned EI_CLASS = 4;
static constexpr unsigned EI_DATA = 5;

static constexpr unsigned ELFMAG0 = 0x7f;
static constexpr unsigned ELFMAG1 = 'E';
static constexpr unsigned ELFMAG2 = 'L';
static constexpr unsigned ELFMAG3 = 'F';

static constexpr unsigned ELFCLASS32 = 1;
static constexpr unsigned ELFCLASS64 = 2;

static constexpr unsigned ELFDATA2LSB = 1;
static constexpr unsigned ELFDATA2MSB = 2;

uint64_t ElfSectionHeader::entries() const { return sh_size / sh_entsize; }

class ElfCommonBase : public ElfFile {
    FILE *fp;

  public:
    ElfCommonBase(FILE *fp) : fp(fp) {}
    ~ElfCommonBase() { fclose(fp); }

    virtual bool setup() = 0;

  protected:
    bool read(size_t offset, size_t size, void *output) const;
};

bool ElfCommonBase::read(size_t offset, size_t size, void *output) const
{
    if (fseek(fp, offset, SEEK_SET) == -1)
        return false;

    if (fread(output, 1, size, fp) != size)
        return false;

    return true;
}

static uint64_t read_integer_le(const void *vp, size_t size)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(vp);
    uint64_t val = 0;
    for (size_t i = 0; i < size; i++)
        val |= (uint64_t)p[i] << (8 * i);
    return val;
}

static uint64_t read_integer_be(const void *vp, size_t size)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(vp);
    uint64_t val = 0;
    for (size_t i = 0; i < size; i++)
        val |= (uint64_t)p[size - 1 - i] << (8 * i);
    return val;
}

template <uint64_t (*readint)(const void *, size_t), bool big>
struct ByteOrder {
    static constexpr bool is_big_endian = big;
    static uint64_t get(const void *p, size_t size) { return readint(p, size); }
    template <typename T> static uint64_t get(const T **p, size_t size)
    {
        uint64_t toret = get(*p, size);
        *p = reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(*p) +
                                         size);
        return toret;
    }
};

template <class ByteOrder, unsigned AddrSize>
class ElfCommon : public ElfCommonBase {
    ElfHeader hdr;

    using ElfCommonBase::ElfCommonBase;

    bool read_header()
    {
        uint8_t data[40 + 3 * AddrSize];
        if (!read(0, sizeof(data), data))
            return false;

        memcpy(hdr.e_ident, data + 0, 16);
        const uint8_t *p = data + 16;
        hdr.e_type = ByteOrder::get(&p, 2);
        hdr.e_machine = ByteOrder::get(&p, 2);
        hdr.e_version = ByteOrder::get(&p, 4);
        hdr.e_entry = ByteOrder::get(&p, AddrSize);
        hdr.e_phoff = ByteOrder::get(&p, AddrSize);
        hdr.e_shoff = ByteOrder::get(&p, AddrSize);
        hdr.e_flags = ByteOrder::get(&p, 4);
        hdr.e_ehsize = ByteOrder::get(&p, 2);
        hdr.e_phentsize = ByteOrder::get(&p, 2);
        hdr.e_phnum = ByteOrder::get(&p, 2);
        hdr.e_shentsize = ByteOrder::get(&p, 2);
        hdr.e_shnum = ByteOrder::get(&p, 2);
        hdr.e_shstrndx = ByteOrder::get(&p, 2);
        assert(p == data + sizeof(data));
        return true;
    }

    bool read_section_header(uint64_t offset, ElfSectionHeader &shdr) const
    {
        uint8_t data[16 + 6 * AddrSize];
        if (!read(offset, sizeof(data), data))
            return false;

        const uint8_t *p = data;
        shdr.sh_name = ByteOrder::get(&p, 4);
        shdr.sh_type = ByteOrder::get(&p, 4);
        shdr.sh_flags = ByteOrder::get(&p, AddrSize);
        shdr.sh_addr = ByteOrder::get(&p, AddrSize);
        shdr.sh_offset = ByteOrder::get(&p, AddrSize);
        shdr.sh_size = ByteOrder::get(&p, AddrSize);
        shdr.sh_link = ByteOrder::get(&p, 4);
        shdr.sh_info = ByteOrder::get(&p, 4);
        shdr.sh_addralign = ByteOrder::get(&p, AddrSize);
        shdr.sh_entsize = ByteOrder::get(&p, AddrSize);
        assert(p == data + sizeof(data));
        return true;
    }

    // Symtab entry format is so different that it has to be devolved
    // to the 32/64 bit specific subclasses
    virtual bool read_symbol(uint64_t offset, ElfSymbol &sym) const = 0;

    bool setup() override
    {
        if (!read_header())
            return false;
        return true;
    }

  public:
    bool is_big_endian() const override { return ByteOrder::is_big_endian; }

    unsigned nsections() const override { return hdr.e_shnum; }

    bool section_header(unsigned index, ElfSectionHeader &out) const override
    {
        if (index >= hdr.e_shnum)
            return false;
        return read_section_header(hdr.e_shoff + hdr.e_shentsize * index, out);
    }

    bool symbol(const ElfSectionHeader &shdr, unsigned symbolindex,
                ElfSymbol &out) const override
    {
        if (symbolindex >= shdr.entries())
            return false;
        return read_symbol(shdr.sh_offset + shdr.sh_entsize * symbolindex, out);
    }

    string strtab_string(const ElfSectionHeader &shdr,
                         unsigned offset) const override
    {
        ostringstream os;
        while (offset < shdr.sh_size) {
            char c;
            if (!read(shdr.sh_offset + offset++, 1, &c))
                break;
            if (c == '\0')
                break;
            os << c;
        }
        return os.str();
    }
};

template <class ByteOrder> class Elf32 : public ElfCommon<ByteOrder, 4> {
    using ElfCommon<ByteOrder, 4>::ElfCommon;

    bool read_symbol(uint64_t offset, ElfSymbol &sym) const override
    {
        uint8_t data[16];
        if (!ElfCommonBase::read(offset, sizeof(data), data))
            return false;

        const uint8_t *p = data;
        sym.st_name = ByteOrder::get(&p, 4);
        sym.st_value = ByteOrder::get(&p, 4);
        sym.st_size = ByteOrder::get(&p, 4);
        uint8_t st_info = ByteOrder::get(&p, 1);
        uint8_t st_other = ByteOrder::get(&p, 1);
        sym.st_shndx = ByteOrder::get(&p, 2);
        assert(p == data + sizeof(data));

        sym.st_bind = st_info >> 4;
        sym.st_type = st_info & 0xF;
        sym.st_visibility = st_other & 0x3;

        return true;
    }
};

template <class ByteOrder> class Elf64 : public ElfCommon<ByteOrder, 8> {
    using ElfCommon<ByteOrder, 8>::ElfCommon;

    bool read_symbol(uint64_t offset, ElfSymbol &sym) const override
    {
        uint8_t data[24];
        if (!ElfCommonBase::read(offset, sizeof(data), data))
            return false;

        const uint8_t *p = data;
        sym.st_name = ByteOrder::get(&p, 4);
        uint8_t st_info = ByteOrder::get(&p, 1);
        uint8_t st_other = ByteOrder::get(&p, 1);
        sym.st_shndx = ByteOrder::get(&p, 2);
        sym.st_value = ByteOrder::get(&p, 8);
        sym.st_size = ByteOrder::get(&p, 8);
        assert(p == data + sizeof(data));

        sym.st_bind = st_info >> 4;
        sym.st_type = st_info & 0xF;
        sym.st_visibility = st_other & 0x3;

        return true;
    }
};

using ByteOrderLE = ByteOrder<read_integer_le, false>;
using ByteOrderBE = ByteOrder<read_integer_be, true>;

using Elf32LE = Elf32<ByteOrderLE>;
using Elf32BE = Elf32<ByteOrderBE>;
using Elf64LE = Elf64<ByteOrderLE>;
using Elf64BE = Elf64<ByteOrderBE>;

std::unique_ptr<ElfFile> elf_open(const string &filename)
{
    FILE *fp = fopen_wrapper(filename.c_str(), "rb");
    if (!fp)
        return nullptr;

    char header[16];
    if (fread(header, 1, 16, fp) != 16) {
        fclose(fp);
        return nullptr;
    }

    if (header[EI_MAG0] != ELFMAG0 || header[EI_MAG1] != ELFMAG1 ||
        header[EI_MAG2] != ELFMAG2 || header[EI_MAG3] != ELFMAG3) {
        fclose(fp);
        return nullptr;
    }

    bool elf64;
    if (header[EI_CLASS] == ELFCLASS32) {
        elf64 = false;
    } else if (header[EI_CLASS] == ELFCLASS64) {
        elf64 = true;
    } else {
        fclose(fp);
        return nullptr;
    }

    bool be;
    if (header[EI_DATA] == ELFDATA2LSB) {
        be = false;
    } else if (header[EI_DATA] == ELFDATA2MSB) {
        be = true;
    } else {
        fclose(fp);
        return nullptr;
    }

    std::unique_ptr<ElfCommonBase> elf_file;
    if (!elf64) {
        if (be)
            elf_file = std::make_unique<Elf32BE>(fp);
        else
            elf_file = std::make_unique<Elf32LE>(fp);
    } else {
        if (be)
            elf_file = std::make_unique<Elf64BE>(fp);
        else
            elf_file = std::make_unique<Elf64LE>(fp);
    }

    if (!elf_file->setup())
        return nullptr; // now fp will be auto-closed by destructor

    return elf_file;
}
