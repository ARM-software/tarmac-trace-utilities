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

#include "libtarmac/registers.hh"
#include "libtarmac/misc.hh"

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>

using std::dec;
using std::ostream;
using std::ostringstream;
using std::string;

struct RegPrefixInfo {
    const char *name;
    size_t namelen;
    unsigned size, disp, n, offset;
};

#define MAKE_REGOFFSET_ENUM_ADVANCE(id, size, disp, n)                         \
    offset_##id, last_##id = offset_##id + disp * n - 1,
#define MAKE_REGOFFSET_ENUM_NOADVANCE(id, size, disp, n)                       \
    offset_##id, last_##id = offset_##id - 1,
enum {
    REGPREFIXLIST(MAKE_REGOFFSET_ENUM_ADVANCE, MAKE_REGOFFSET_ENUM_NOADVANCE)
};
#undef MAKE_REGOFFSET_ENUM

#define NO_OFFSET UINT_MAX

#define MAKE_REGPREFIX_INFO(id, size, disp, n)                                 \
    {#id, sizeof(#id) - 1, size, disp, n, offset_##id},
static const RegPrefixInfo reg_prefixes[] = {
    REGPREFIXLIST(MAKE_REGPREFIX_INFO, MAKE_REGPREFIX_INFO)};
#undef MAKE_REGPREFIX_INFO

ostream &operator<<(ostream &os, const RegisterId &id)
{
    const RegPrefixInfo &pfx = reg_prefixes[(size_t)id.prefix];
    os << pfx.name;
    if (pfx.n > 1)
        os << dec << id.index;
    return os;
}

static inline bool compare(const char *a, size_t alen, const char *b,
                           size_t blen)
{
    return alen == blen && !strncasecmp(a, b, alen);
}

#define STRINGWITHLEN(s) s, sizeof(s) - 1
#include <iostream>
bool lookup_reg_name(RegisterId &out, const string &name)
{
    const char *prefix = name.c_str();
    const char *suffix = prefix + strcspn(prefix, "0123456789_");

    for (size_t i = 0; i < lenof(reg_prefixes); i++) {
        if (i == (size_t)RegPrefix::internal_flags)
            continue; // this isn't a real register name

        const RegPrefixInfo &pfx = reg_prefixes[i];
        if (compare(prefix, suffix - prefix, pfx.name, pfx.namelen)) {
            unsigned long index = 0;
            if (!*suffix) {
                /*
                 * Accept a register name without a numeric suffix
                 * only if the register class is a singleton.
                 */
                std::cerr << "empty suffix " << pfx.name << " has " << pfx.n
                          << "\n";
                if (pfx.n != 1)
                    continue;
                index = 0;
            } else {
                /*
                 * Accept a register name _with_ a numeric suffix only
                 * if the register class is _not_ a singleton.
                 * Moreover, the suffix should be in range.
                 */
                if (pfx.n == 1)
                    continue;
                index = strtoul(suffix, NULL, 10);
                if (index >= pfx.n)
                    continue;
            }
            out.prefix = RegPrefix(i);
            out.index = index;
            return true;
        }
    }

    /*
     * Some aliases.
     */
    if (compare(prefix, suffix - prefix, STRINGWITHLEN("msp"))) {
        out.prefix = RegPrefix::r;
        out.index = 13;
        return true;
    }
    if (compare(prefix, suffix - prefix, STRINGWITHLEN("e"))) {
        /*
         * One flavour of Tarmac I've seen appears to render the
         * AArch64 x-registers as e0,e1,... instead of x0,x1,...
         */
        out.prefix = RegPrefix::x;
        out.index = atoi(suffix);
        return true;
    }
    if (compare(prefix, suffix - prefix, STRINGWITHLEN("lr"))) {
        out.prefix = RegPrefix::r;
        out.index = 14;
        return true;
    }
    if (compare(prefix, suffix - prefix, STRINGWITHLEN("cpsr"))) {
        out.prefix = RegPrefix::psr;
        out.index = 0;
        return true;
    }

    return false;
}

string reg_name(const RegisterId &reg)
{
    ostringstream os;
    os << reg;
    return os.str();
}

bool reg_needs_iflags(RegPrefix prefix)
{
    const RegPrefixInfo &pfx = reg_prefixes[(size_t)prefix];
    return pfx.disp == 0;
}

bool reg_needs_iflags(const RegisterId &reg)
{
    return reg_needs_iflags(reg.prefix);
}

Addr reg_offset(const RegisterId &reg)
{
    const RegPrefixInfo &pfx = reg_prefixes[(size_t)reg.prefix];
    assert(pfx.disp != 0);
    return pfx.offset + reg.index * pfx.disp;
}

Addr reg_offset(const RegisterId &reg, unsigned iflags)
{
    const RegPrefixInfo &pfx = reg_prefixes[(size_t)reg.prefix];
    if (reg.prefix == RegPrefix::s || reg.prefix == RegPrefix::d) {
        const RegPrefixInfo &vpfx = reg_prefixes[(size_t)RegPrefix::q];
        if (iflags & IFLAG_AARCH64)
            return vpfx.offset + reg.index * vpfx.disp;
        else
            return vpfx.offset + reg.index * pfx.size;
    } else {
        return reg_offset(reg);
    }
}

size_t reg_size(const RegisterId &reg)
{
    const RegPrefixInfo &pfx = reg_prefixes[(size_t)reg.prefix];
    return pfx.size;
}

const RegisterId REG_iflags = {RegPrefix::internal_flags, 0};
const RegisterId REG_32_sp = {RegPrefix::r, 13};
const RegisterId REG_32_lr = {RegPrefix::r, 14};
const RegisterId REG_32_r0 = {RegPrefix::r, 0};
const RegisterId REG_32_r1 = {RegPrefix::r, 1};
const RegisterId REG_64_xsp = {RegPrefix::xsp, 0};
const RegisterId REG_64_xlr = {RegPrefix::x, 30};
const RegisterId REG_64_x0 = {RegPrefix::x, 0};
const RegisterId REG_64_x1 = {RegPrefix::x, 1};
