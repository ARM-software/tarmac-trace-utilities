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

#ifndef LIBTARMAC_IMAGE_HH
#define LIBTARMAC_IMAGE_HH

#include "libtarmac/elf.hh"
#include "libtarmac/misc.hh"

#include <forward_list>
#include <map>
#include <string>
#include <vector>

struct Symbol {
    enum class binding_type { any = 0, local, global };
    enum class kind_type { any = 0, object, function };

    Addr addr;
    size_t size;
    bool multiple = false; // are there multiple symbols with this name?
    binding_type binding; // can be used for smarter symbol lookup
    kind_type kind;       // can be used for smarter symbol lookup

    Symbol(Addr addr, size_t size, const std::string &name,
           binding_type binding, kind_type kind)
        : addr(addr), size(size), binding(binding), kind(kind), name(name)
    {
    }
    std::string getName() const;

    bool operator<(const Symbol &other) const { return name < other.name; }

  private:
    // This is private so that clients call getName() instead, which
    // annotates the symbol name to resolve ambiguities.
    std::string name;

    // But Image is allowed to access it, to set these structures up
    // in the first place.
    friend class Image;
};

struct Segment {
    Addr addr;
    size_t size;
    bool executable;
    bool writable;
    bool readable;

    // Get a friendly kind name for the segment kind.
    const char *getKindName() const;

    Segment(Addr addr, size_t size, bool readable, bool writable,
            bool executable)
        : addr(addr), size(size), writable(writable), executable(executable),
          readable(readable)
    {
    }
};

class Image {
    std::unique_ptr<ElfFile> elf_file;
    const std::string image_filename;
    bool is_big_end;
    std::forward_list<Symbol> symbols;
    std::map<Addr, std::vector<const Symbol *>> addrtab;
    std::map<std::string, std::vector<const Symbol *>> symtab;
    mutable std::vector<Segment> segments;

    void add_symbol(const Symbol &sym);
    void load_headers();
    void load_symboltable();

  public:
    const std::string &get_filename() const { return image_filename; }
    bool is_big_endian() const { return is_big_end; }

    // Return the 'best' symbol that represent the given address
    // Returns NULL if no symbol matches
    const Symbol *find_symbol(Addr address) const;
    // Return the symbol for the given name
    const Symbol *find_symbol(const std::string &name) const;
    const Symbol *find_symbol(const std::string &name, int index) const;
    // Return all symbols with the given name
    const std::vector<const Symbol *> *
    find_all_symbols(const std::string &name) const
    {
        auto res = this->symtab.find(name);
        return (res == this->symtab.end() ? nullptr : &res->second);
    }
    std::vector<const Symbol *>
    find_all_symbols_starting_with(const std::string &name) const
    {
        std::vector<const Symbol *> res;
        for (const auto &it : this->symtab)
            if (it.first.rfind(name, 0) == 0)
                res.insert(res.end(), it.second.begin(), it.second.end());
        return res;
    }

    const std::vector<Segment> &get_segments() const;

    Image(const std::string &image_filename);
    ~Image();

    void dump(); // debug function
};

#endif // LIBTARMAC_IMAGE_HH
