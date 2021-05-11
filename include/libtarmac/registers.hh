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

#ifndef LIBTARMAC_REGISTERS_HH
#define LIBTARMAC_REGISTERS_HH

#include "libtarmac/misc.hh"

#include <ostream>
#include <string>

/*
 * List macro giving the known general register _classes_. For each
 * one, we call one of the parameter macros X or Y with four
 * arguments: the identifier string, the size of each register in
 * bytes, the displacement from each register's starting point to the
 * next, and number of registers in this class that exist. The macro X
 * reserves space in the address map, whereas Y doesn't advance the
 * current position, so that registers defined by Y will alias
 * whatever register class is defined next.
 *
 * A special case is that if the displacement is defined as 0, the
 * register class is taken to be special enough that its offsets are
 * computed in custom C++ code, and the enumeration here just says how
 * many of them there are and their size.
 */
#define REGPREFIXLIST(X, Y)                                                    \
    Y(r, 4, 8, 16)                                                             \
    Y(w, 4, 8, 31)                                                             \
    X(x, 8, 8, 31)                                                             \
    Y(wsp, 4, 8, 1)                                                            \
    X(xsp, 8, 8, 1)                                                            \
    Y(v, 16, 16, 32)                                                           \
    X(q, 16, 16, 32)                                                           \
    Y(d, 8, 0, 32) /* dN and sN overlap qN differently */                      \
    Y(s, 4, 0, 32) /*      between AArch64 and AArch32 */                      \
    X(psr, 4, 4, 1)                                                            \
    X(fpscr, 4, 4, 1)                                                          \
    X(fpcr, 4, 4, 1)                                                           \
    X(fpsr, 4, 4, 1)                                                           \
    X(vpr, 4, 4, 1)                                                            \
                                                                               \
    /* Fake 'register' used by the Tarmac indexer to store state               \
     * information that isn't reflected in any register updates shown          \
     * in the trace, such as whether we're currently in AArch32 or             \
     * AArch64 mode. */                                                        \
    X(internal_flags, 4, 4, 1)                                                 \
    /* end of list */

#define MAKE_REGPREFIX_ENUM(id, size, disp, n) id,
enum class RegPrefix {
    REGPREFIXLIST(MAKE_REGPREFIX_ENUM, MAKE_REGPREFIX_ENUM)
};
#undef MAKE_REGPREFIX_ENUM

struct RegisterId {
    RegPrefix prefix;
    unsigned index;
    inline bool operator==(const RegisterId &rhs) const
    {
        return prefix == rhs.prefix && index == rhs.index;
    }
    inline bool operator!=(const RegisterId &rhs) const
    {
        return !(*this == rhs);
    }
};

std::ostream &operator<<(std::ostream &os, const RegisterId &id);

bool lookup_reg_name(RegisterId &out, const std::string &name);

std::string reg_name(const RegisterId &reg);
size_t reg_size(const RegisterId &reg);

// In order to look up some kinds of register class, the contents of
// the internal_flags register are required, because their offsets
// vary with the flags. For example, the 's' registers overlap 'd' in
// a different way between AArch32 and AArch64, so you have to know
// the current iflags in order to figure out which one you want.
//
// The unary reg_offset fails an assertion if you pass it a register
// id that can't be looked up without the iflags. To check in advance,
// use reg_needs_iflags() below.
Addr reg_offset(const RegisterId &reg);
Addr reg_offset(const RegisterId &reg, unsigned internal_flags);
bool reg_needs_iflags(RegPrefix pfx);
bool reg_needs_iflags(const RegisterId &reg);

/*
 * Bit values for the 'internal_flags' fake register.
 */
#define IFLAG_AARCH64 1 /* whether we're currently in AArch32 or AArch64 */
#define IFLAG_BIGEND 2  /* little- or big-endian? */

/*
 * Useful standard register ids.
 */
extern const RegisterId REG_iflags;
extern const RegisterId REG_32_sp;
extern const RegisterId REG_32_lr;
extern const RegisterId REG_32_r0;
extern const RegisterId REG_32_r1;
extern const RegisterId REG_64_xsp;
extern const RegisterId REG_64_xlr;
extern const RegisterId REG_64_x0;
extern const RegisterId REG_64_x1;

#endif // LIBTARMAC_REGISTERS_HH
