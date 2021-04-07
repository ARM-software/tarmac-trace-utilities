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

#include "callinfo.hh"
#include "calltree.hh"

#include "libtarmac/image.hh"

#include <iostream>

using std::cout;
using std::dec;
using std::hex;
using std::size_t;
using std::string;
using std::vector;

static bool isHexString(const string &str, Addr &addr)
{
    if (str.size() < 3 || str[0] != '0' || str[1] != 'x')
        return false;
    size_t found = str.find_first_not_of("0123456789abcdefABCDEF", 2);
    if (found != string::npos)
        return false;
    addr = stoull(str, nullptr, 16);
    return true;
}

void CallInfo::run(const vector<string> &functions)
{
    for (const auto &F : functions) {
        // If this looks like an hexadecimal , let's convert it to an address.
        Addr addr;
        if (isHexString(F, addr))
            run(addr);
        else
            run(F);
    }
}

void CallInfo::run(Addr symb_addr)
{
    vector<TarmacSite> Sites;
    unsigned long long pc = symb_addr;
    pc &= ~(unsigned long long)1;

    // Search first in the bypctree to get the times at which symbol is called.
    {
        ByPCPayload ByPCFinder, ByPCFound;
        ByPCFinder.pc = pc;
        ByPCFinder.trace_file_firstline = 0;

        while (true) {
            bool ret = index.bypctree.succ(index.bypcroot, ByPCFinder,
                                           &ByPCFound, nullptr);

            if (ret && ByPCFound.pc == pc &&
                ByPCFound.trace_file_firstline >
                    ByPCFinder.trace_file_firstline) {
                Sites.push_back(
                    TarmacSite(ByPCFound.pc, ByPCFound.trace_file_firstline));
                ByPCFinder.trace_file_firstline =
                    ByPCFound.trace_file_firstline + 1;
            } else
                break;
        }
    }

    // Search in seqtree, where we have much more details.
    for (auto &s : Sites) {
        SeqOrderPayload SeqOrderFinder, SeqOrderFound;
        SeqOrderFinder.pc = s.addr;
        SeqOrderFinder.trace_file_firstline = s.tarmac_line;

        if (index.seqtree.find(index.seqroot, SeqOrderFinder, &SeqOrderFound,
                               nullptr)) {
            s.tarmac_pos = SeqOrderFound.trace_file_pos;
            s.time = SeqOrderFound.mod_time;
        }
    }

    for (const auto &s : Sites)
        cout << " - time: " << s.time
             << " (line:" << (s.tarmac_line + index.lineno_offset)
             << ", pos:" << s.tarmac_pos << ")\n";
}

void CallInfo::run(const string &symb_name)
{
    if (!has_image()) {
        cout << "No image, symbol '" << symb_name
             << "' can not be looked up !\n";
        return;
    }

    uint64_t symb_addr;
    size_t symb_size;
    if (!lookup_symbol(symb_name, symb_addr, symb_size)) {
        cout << "Symbol '" << symb_name << "' not found !\n";
        return;
    }

    cout << "Symbol '" << symb_name << "' at 0x" << hex << symb_addr << dec
         << " (" << symb_size << " bytes) called from :\n";

    run(symb_addr);
}
