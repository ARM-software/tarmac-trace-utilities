/*
 * Copyright 2016-2021,2025 Arm Limited. All rights reserved.
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

#include "libtarmac/image.hh"
#include "libtarmac/intl.hh"
#include "libtarmac/elf.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/reporter.hh"

#include <cstdio>
#include <sstream>
#include <string>

using std::ostringstream;
using std::string;
using std::vector;

string Symbol::getName() const
{
    if (multiple) {
        ostringstream oss;
        oss << name << "@0x" << std::hex << addr;
        return oss.str();
    }
    return name;
}

static inline bool want_to_index_symbol(string name)
{
    // Don't index anonymous symbols.
    if (name.empty())
        return false;

    // Don't index Arm mapping symbols.
    //
    // These are symbols beginning with '$a', '$t', '$x' or '$d', and
    // their job is to tell a static disassembler which parts of the
    // file they should be disassembling as Arm, Thumb or data (in
    // AArch32), or as code or data (in AArch64). They're extremely
    // common and often have identical names, so they're never useful
    // as a means of identifying a particular address.
    //
    // For more information on mapping symbols see the AAELF32 spec:
    // https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst#mapping-symbols
    if (name.size() >= 2 && name[0] == '$' &&
        (name[1] == 'a' || name[1] == 't' ||
         name[1] == 'x' || name[1] == 'd'))
        return false;

    // Index everything else.
    return true;
}

void Image::add_symbol(const Symbol &sym_)
{
    // store the symbol
    symbols.push_front(sym_);

    // Ensure we got the address to the symbol now stored
    Symbol &sym = symbols.front();

    // address -> symbol map
    auto &syms = addrtab[sym.addr];
    syms.push_back(&sym);

    // name -> symbol map
    auto &dups = symtab[sym.name];
    if (dups.size() > 0) {
        // There's already at least one symbol with this name. Mark
        // the new one as multiple, meaning it will need
        // disambiguation when the address is printed later.
        sym.multiple = true;
        if (dups.size() == 1) {
            // And if there was previously only one symbol with the
            // same name, then that one isn't yet marked as multiple,
            // so do that too.
            //
            // The symbol pointer in 'dups' is a const pointer, but we
            // know it's also an element of the 'symbols' list, which
            // we're allowed to mutate in this function (indeed we
            // already have done). So it's safe to cast away the const
            // and modify it.
            const_cast<Symbol *>(dups.front())->multiple = true;
        }
    }
    dups.push_back(&sym);
}

void Image::load_headers() { is_big_end = elf_file->is_big_endian(); }

void Image::load_symboltable()
{
    for (unsigned i = 0, e = elf_file->nsections(); i < e; i++) {
        ElfSectionHeader shdr;
        if (!elf_file->section_header(i, shdr) || shdr.sh_type != SHT_SYMTAB)
            continue;

        ElfSectionHeader strtab_shdr;
        if (!elf_file->section_header(shdr.sh_link, strtab_shdr) ||
            strtab_shdr.sh_type != SHT_STRTAB)
            continue;

        for (unsigned j = 0, e = shdr.entries(); j < e; j++) {
            ElfSymbol sym;
            if (!elf_file->symbol(shdr, j, sym))
                continue;

            Symbol::binding_type binding;
            switch (sym.st_bind) {
            default:
                // skip the symbol
                continue;
            case STB_LOCAL:
                binding = Symbol::binding_type::local;
                break;
            case STB_GLOBAL:
            case STB_WEAK:
                binding = Symbol::binding_type::global;
                break;
            }

            Symbol::kind_type kind;
            switch (sym.st_type) {
            default:
                // skip the symbol
                continue;
            case STT_NOTYPE:
                kind = Symbol::kind_type::any;
                break;
            case STT_OBJECT:
                kind = Symbol::kind_type::object;
                break;
            case STT_FUNC:
                kind = Symbol::kind_type::function;
                break;
            }

            string symbol_name =
                elf_file->strtab_string(strtab_shdr, sym.st_name);
            if (want_to_index_symbol(symbol_name))
                add_symbol(Symbol(static_cast<Addr>(sym.st_value), sym.st_size,
                                  symbol_name, binding, kind));
        }
    }
}

const Symbol *Image::find_symbol(Addr address) const
{
    // Instead of seeing symbols as object with a size, we use them as
    // labels. We give priority to symbols with a size. This should
    // work reasonable well given the assumption that objects don't
    // have much overlap with each other.

    const Symbol *backup_sym = nullptr;
    auto it = addrtab.upper_bound(address);

    // 'address' is before all symbols
    if (it == addrtab.begin())
        return nullptr;

    // find the first symbol that contains or starts before 'address'
    for (--it; it != addrtab.begin(); --it) {
        const vector<const Symbol *> &syms = it->second;

        for (const Symbol *sym : syms) {
            if (sym->size != 0 && address - sym->addr < sym->size)
                return sym;
            if (!backup_sym)
                backup_sym = sym;
        }
    }
    return backup_sym;
}

const Symbol *Image::find_symbol(const string &name) const
{
    size_t pos = name.rfind('#');
    int index = 0;
    if (pos != string::npos) {
        try {
            index = stoi(name.substr(pos + 1));
        } catch (std::out_of_range) {
            return nullptr;
        } catch (std::invalid_argument) {
            return nullptr;
        }
        return find_symbol(name.substr(0, pos), index);
    }
    return find_symbol(name, index);
}

const Symbol *Image::find_symbol(const string &name, int index) const
{
    const vector<const Symbol *> *res = find_all_symbols(name);
    if (res)
        try {
            return res->at(index);
        } catch (std::out_of_range e) {
        }
    return nullptr;
}

vector<Segment> Image::get_segments(bool use_paddr) const
{
    vector<Segment> segments;
    for (unsigned idx = 0; idx < elf_file->nsegments(); idx++) {
        ElfProgramHeader phdr;
        if (elf_file->program_header(idx, phdr))
            segments.emplace_back(idx, use_paddr ? phdr.p_paddr : phdr.p_vaddr,
                                  phdr.p_memsz, phdr.p_filesz,
                                  phdr.p_flags & PF_R, phdr.p_flags & PF_W,
                                  phdr.p_flags & PF_X);
    }

    return segments;
}

vector<uint8_t> Image::get_segment_content(const Segment &segment) const
{
    vector<uint8_t> content;

    if (!elf_file->segment_loadable_content(segment.index, content))
        content.clear();

    return content;
}

Image::Image(const string &image_filename) : image_filename(image_filename)
{
    elf_file = elf_open(image_filename);
    if (!elf_file)
        reporter->errx(1, _("Cannot open ELF file \"%s\""),
                       image_filename.c_str());
    load_headers();
    load_symboltable();
}

Image::~Image() {}

void Image::dump()
{
    printf(_("Image '%s':\n"), image_filename.c_str());
    for (const auto &sym : symbols) {
        std::string name = sym.getName();
        printf(_("symbol '%s' [0x%llx, 0x%llx)\n"), name.c_str(),
               sym.addr, sym.addr + sym.size);
    }
}

#ifdef TEST
int main(int argc, char *argv[])
{
    Image image{string(argv[1])};
    image.dump();
}
#endif
