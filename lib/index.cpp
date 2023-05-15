/*
 * Copyright 2016-2023 Arm Limited. All rights reserved.
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

#include "libtarmac/index.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/registers.hh"
#include "libtarmac/reporter.hh"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

using std::cout;
using std::dec;
using std::endl;
using std::exception;
using std::hex;
using std::ifstream;
using std::ios;
using std::make_pair;
using std::make_unique;
using std::max;
using std::min;
using std::ostringstream;
using std::pair;
using std::ref;
using std::set;
using std::showbase;
using std::streampos;
using std::string;
using std::vector;

struct PendingCall {
    unsigned long long sp, pc;
    unsigned call_line;
    PendingCall(unsigned long long sp, unsigned long long pc,
                unsigned call_line = 0)
        : sp(sp), pc(pc), call_line(call_line)
    {
    }

    bool operator<(const PendingCall &rhs) const
    {
        return (sp != rhs.sp ? sp < rhs.sp
                             : pc != rhs.pc ? pc < rhs.pc : false);
    }
};

struct CallReturn {
    unsigned line;
    int direction; // +1 = call, -1 = return
    CallReturn(unsigned line, int direction) : line(line), direction(direction)
    {
    }

    bool operator<(const CallReturn &rhs) const
    {
        return (line != rhs.line ? line < rhs.line : false);
    }
};

class Index : ParseReceiver {
    string tarmac_filename, index_filename;
    OFF_T last_memroot, memroot, seqroot;
    unsigned long long last_sp, curr_sp, curr_pc, insns_since_lr_update;
    unsigned long long expected_next_pc, expected_next_lr;
    MMapFile *index_mmap;
    AVLDisk<MemoryPayload, MemoryAnnotation> *memtree;
    AVLDisk<MemorySubPayload> *memsubtree;
    AVLDisk<SeqOrderPayload, SeqOrderAnnotation> *seqtree;
    Time current_time;
    bool seen_instruction_at_current_time;
    set<PendingCall> pending_calls;
    set<CallReturn> found_callrets;
    bool bigend;
    bool aarch64_used;
    ISet last_iset;
    unsigned curr_iflags;

    void delete_from_memtree(char type, Addr addr, size_t size);

    // Used during parsing (shared between parse_tarmac_line and
    // got_event):
    size_t lineno, true_lineno, lineno_offset, prev_lineno;
    bool seen_any_event;
    streampos linepos, oldpos;
    AVLDisk<ByPCPayload> *bypctree;
    OFF_T bypcroot;

    unsigned char *make_memtree_update(char type, Addr addr, size_t size);

    inline const RegisterId &REG_sp()
    {
        return (curr_iflags & IFLAG_AARCH64) ? REG_64_xsp : REG_32_sp;
    }
    inline const RegisterId &REG_lr()
    {
        return (curr_iflags & IFLAG_AARCH64) ? REG_64_xlr : REG_32_lr;
    }

  public:
    Index(string index_filename, bool bigend)
        : index_filename(index_filename), expected_next_pc(KNOWN_INVALID_PC),
          index_mmap(nullptr), memtree(nullptr), memsubtree(nullptr),
          seqtree(nullptr), bigend(bigend), aarch64_used(false), last_iset(ARM)
    {
    }

    ~Index()
    {
        if (index_mmap)
            delete index_mmap;
        if (memtree)
            delete memtree;
        if (memsubtree)
            delete memsubtree;
        if (seqtree)
            delete seqtree;
        if (bypctree)
            delete bypctree;
    }

    void got_event_common(TarmacEvent *event, bool is_instruction);
    bool parse_warning(const string &msg);
    TarmacEvent *parse_tarmac_line(string line);
    void parse_tarmac_file(string tarmac_filename);
    OFF_T make_sub_memtree(char type, Addr addr, size_t size);
    void update_memtree(char type, Addr addr, size_t size,
                        unsigned long long contents);
    void update_memtree_if_necessary(char type, Addr addr, size_t size,
                                     unsigned long long contents);
    void update_memtree_from_read(char type, Addr addr, size_t size,
                                  unsigned long long contents);
    bool read_memtree_value(char type, Addr addr, size_t size,
                            unsigned long long *output);
    bool read_memtree_reg(const RegisterId &reg, unsigned long long *output);
    void update_sp(unsigned long long sp);
    void update_pc(unsigned long long pc, unsigned long long next_pc,
                   ISet iset);
    void update_iflags(unsigned iflags);
    bool is_bigendian() const { return bigend; }
    void got_event(RegisterEvent &ev);
    void got_event(MemoryEvent &ev);
    void got_event(InstructionEvent &ev);
    void got_event(TextOnlyEvent &ev);
};

void Index::update_sp(unsigned long long sp)
{
    curr_sp = sp;

    for (auto it = pending_calls.begin(); it != pending_calls.end();) {
        if (it->sp >= sp)
            break;

        auto to_erase = it;
        ++it;
        pending_calls.erase(to_erase);
    }
}

void Index::update_pc(unsigned long long pc, unsigned long long next_pc,
                      ISet iset)
{
    if (iset == A64)
        aarch64_used = true;

    if (((pc ^ expected_next_pc) & ~1ULL) != 0) {
        // The last instruction transferred control to somewhere other
        // than the obvious next memory location. See if the obvious
        // next location - or something near it - was also left in lr.
        // If so, this is potentially a call instruction, and we
        // should record its details in case we later see something we
        // can identify as the matching return. Alternatively, this
        // might _be_ a return that we can match with a previous call.

        unsigned long long lr, sp;
        if (!read_memtree_reg(REG_sp(), &sp))
            sp = ULLONG_MAX;

#ifdef DEBUG_CALL_HEURISTICS
        cout << "transfer of control @ " << prev_lineno << ", sp=" << hex << sp
             << ", pc=" << pc << dec << endl;
#endif

        auto it = pending_calls.find(PendingCall(sp, pc));
        if (it != pending_calls.end()) {

#ifdef DEBUG_CALL_HEURISTICS
            cout << "  looks like return for call @ " << it->call_line << endl;
#endif

            // it->call_line is the line number of the first
            // instruction of the called function, and
            // prev_lineno is the first instruction after the
            // return. This is just what we want without having to
            // correct any annoying off-by-1 errors, because if we
            // treat that pair of times as the endpoints of a
            // half-open interval of the usual [a,b) type and
            // exclude everything in that interval, we'll cut out
            // exactly the instructions that are not in the
            // (apparent) sequential execution path of the caller.

            found_callrets.insert(CallReturn(it->call_line, +1));
            found_callrets.insert(CallReturn(prev_lineno, -1));
            pending_calls.erase(it);
        } else if (read_memtree_reg(REG_lr(), &lr) &&
                   insns_since_lr_update < 8 &&
                   absdiff(lr, expected_next_lr) < 64) {

#ifdef DEBUG_CALL_HEURISTICS
            cout << "  inserting as pending call with sp=" << hex << sp
                 << " lr=" << lr << dec << endl;
#endif

            pending_calls.insert(PendingCall(sp, lr, prev_lineno));
        }
    }

    curr_pc = pc;
    expected_next_pc = next_pc;
    expected_next_lr = expected_next_pc | (iset == THUMB ? 1 : 0);
}

void Index::update_iflags(unsigned iflags)
{
    curr_iflags = iflags;
    update_memtree_if_necessary('r', reg_offset(REG_iflags),
                                reg_size(REG_iflags), iflags);
}

static inline bool reg_update_overwrites_reg(Addr offset, size_t size,
                                             const RegisterId &reg,
                                             unsigned iflags)
{
    Addr regoffset = reg_offset(reg, iflags);
    Addr regsize = reg_size(reg);
    return !(offset + size <= regoffset) && !(regoffset + regsize <= offset);
}

void Index::got_event(RegisterEvent &ev)
{
    got_event_common(&ev, false);

    RegisterId reg = ev.reg;

    if (reg.prefix == RegPrefix::s && (curr_iflags & IFLAG_AARCH64)) {
        /*
         * In AArch64, writing an s-register has the side effect of
         * zeroing out the other half of its corresponding d-register.
         * Since the s-reg aliases the LSW of the d-reg (regardless of
         * endianness), this is most easily implemented by just
         * pretending it's a d-reg write.
         */
        reg.prefix = RegPrefix::d;
    }
    auto offset = reg_offset(reg, curr_iflags) + ev.offset;
    auto size = ev.bytes.size();
    unsigned char *p = make_memtree_update('r', offset, size);
    memcpy(p, ev.bytes.data(), size);

    if (reg_update_overwrites_reg(offset, size, REG_sp(), curr_iflags)) {
        unsigned long long new_sp_value;
        if (read_memtree_value('r', reg_offset(REG_sp(), curr_iflags),
                               reg_size(REG_sp()), &new_sp_value))
            update_sp(new_sp_value);
    }

    if (reg_update_overwrites_reg(offset, size, REG_lr(), curr_iflags))
        insns_since_lr_update = 0;
}

void Index::got_event(MemoryEvent &ev)
{
    got_event_common(&ev, false);

    if (!ev.read) {
        if (ev.known)
            update_memtree('m', ev.addr, ev.size, ev.contents);
        else
            make_sub_memtree('m', ev.addr, ev.size);
    } else {
        if (ev.known)
            update_memtree_from_read('m', ev.addr, ev.size, ev.contents);
        // if (read && !known), nothing we can do at all!
    }
}

void Index::got_event(InstructionEvent &ev)
{
    got_event_common(&ev, true);

    insns_since_lr_update++;

    Addr adjusted_pc = ev.pc | (ev.iset == THUMB ? 1 : 0);

    if (ev.executed &&
        ((ev.iset == THUMB && ev.instruction == 0xbeab /* BKPT #0xab */) ||
         (ev.iset == THUMB && ev.instruction == 0xdfab /* SVC #0xab */) ||
         (ev.iset == THUMB && ev.instruction == 0xbabc /* HLT #0x3f */) ||
         (ev.iset == ARM &&
          (ev.instruction & 0x0fffffff) == 0x0f123456 /* SVC #0x123456 */) ||
         (ev.iset == ARM &&
          (ev.instruction & 0x0fffffff) == 0x010f0070 /* HLT #0xF000 */) ||
         (ev.iset == A64 && ev.instruction == 0xD45E0000 /* HLT #0xF000 */))) {
        unsigned long long r0, r1, startaddr, size;
        // Try to read enough of the semihosting parameters to find
        // out what region of memory is potentially overwritten. If
        // anything is already in the 'unknown' state, there's nothing
        // we can do, so just stop trying to be clever and proceed
        // with the rest of the trace anyway.
        RegisterId opreg = ev.iset == A64 ? REG_64_x0 : REG_32_r0;
        RegisterId blkreg = ev.iset == A64 ? REG_64_x1 : REG_32_r1;
        unsigned word = ev.iset == A64 ? 8 : 4;

        if (!read_memtree_reg(opreg, &r0))
            r0 = 0; // a known-harmless value

        switch (r0) {
        case 0x06:
            // SYS_READ: r1 points to a parameter block of which word
            // 1 is buffer start and word 2 is length. (0 is the file
            // handle.)
            if (!read_memtree_reg(blkreg, &r1))
                break;
            if (!read_memtree_value('m', r1 + word, word, &startaddr))
                break;
            if (!read_memtree_value('m', r1 + 2 * word, word, &size))
                break;
            make_sub_memtree('m', startaddr, size);
            break;
        case 0x0D:
            // SYS_TMPNAM: r1 points to a parameter block of which
            // word 0 is buffer start and word 2 is length. (1 is a
            // unique identifier to construct the name.)
            if (!read_memtree_reg(blkreg, &r1))
                break;
            if (!read_memtree_value('m', r1, word, &startaddr))
                break;
            if (!read_memtree_value('m', r1 + 2 * word, word, &size))
                break;
            make_sub_memtree('m', startaddr, size);
            break;
        case 0x15:
            // SYS_GET_CMDLINE: r1 points to a parameter block of
            // which word 0 is buffer start and word 1 is length.
            if (!read_memtree_reg(blkreg, &r1))
                break;
            if (!read_memtree_value('m', r1, word, &startaddr))
                break;
            if (!read_memtree_value('m', r1 + word, word, &size))
                break;
            make_sub_memtree('m', startaddr, size);
            break;
        case 0x16:
            // SYS_HEAPINFO: r1 points to a parameter block of which
            // word 0 (in fact the only word) is the buffer start.
            // Length is fixed at four words.
            if (!read_memtree_reg(blkreg, &r1))
                break;
            if (!read_memtree_value('m', r1, word, &startaddr))
                break;
            make_sub_memtree('m', startaddr, 4 * word);
            break;
        case 0x30:
            // SYS_ELAPSED: r1 is itself the address of buffer start.
            // Length is fixed at two words.
            if (!read_memtree_reg(blkreg, &r1))
                break;
            make_sub_memtree('m', r1, word * 2);
            break;
        default:
            // Any other semihosting call doesn't write memory anyway.
            break;
        }
    }

    unsigned iflags = 0;
    if (ev.iset == A64)
        iflags |= IFLAG_AARCH64;
    if (is_bigendian())
        iflags |= IFLAG_BIGEND;
    update_iflags(iflags);

    update_pc(adjusted_pc, adjusted_pc + ev.width / 8, ev.iset);
}

void Index::got_event(TextOnlyEvent &ev) { got_event_common(&ev, false); }

void Index::delete_from_memtree(char type, Addr addr, size_t size)
{
    MemoryPayload memp;
    memp.type = type;
    memp.lo = addr;
    memp.hi = addr + (size - 1);
    memp.trace_file_firstline = prev_lineno;
    while (true) {
        bool found;
        MemoryPayload old_memp;
        memroot = memtree->remove(memroot, memp, &found, &old_memp);
        if (!found)
            break;
        if (old_memp.lo < memp.lo) {
            MemoryPayload memp_below = old_memp;
            memp_below.hi = memp.lo - 1;
            memroot = memtree->insert(memroot, memp_below);
        }
        if (old_memp.hi > memp.hi) {
            MemoryPayload memp_above = old_memp;
            if (memp_above.raw)
                memp_above.contents =
                    memp_above.contents + (memp.hi + 1 - memp_above.lo);
            memp_above.lo = memp.hi + 1;
            memroot = memtree->insert(memroot, memp_above);
        }
    }
}

unsigned char *Index::make_memtree_update(char type, Addr addr, size_t size)
{
    OFF_T contents_offset = index_mmap->alloc(size);

    delete_from_memtree(type, addr, size);

    MemoryPayload memp;
    memp.type = type;
    memp.lo = addr;
    memp.hi = addr + (size - 1);
    memp.raw = true;
    memp.contents = contents_offset;
    memp.trace_file_firstline = prev_lineno;
    memroot = memtree->insert(memroot, memp);

    return index_mmap->getptr<unsigned char>(contents_offset);
}

void Index::update_memtree(char type, Addr addr, size_t size,
                           unsigned long long contents)
{
    unsigned char *contents_ptr = make_memtree_update(type, addr, size);
    if (type == 'm' && bigend) {
        for (size_t i = 0; i < size; i++)
            contents_ptr[i] = contents >> (8 * (size - 1 - i));
    } else {
        for (size_t i = 0; i < size; i++)
            contents_ptr[i] = contents >> (8 * i);
    }
}

void Index::update_memtree_if_necessary(char type, Addr addr, size_t size,
                                        unsigned long long contents)
{
    /*
     * Wrapper around update_memtree which avoids writing extra data
     * into the index if the value at the specified address hasn't
     * changed anyway.
     */
    unsigned long long prev_contents;
    if (read_memtree_value(type, addr, size, &prev_contents) &&
        prev_contents == contents)
        return;

    update_memtree(type, addr, size, contents);
}

OFF_T Index::make_sub_memtree(char type, Addr addr, size_t size)
{
    OFF_T newroot_offset = index_mmap->alloc(sizeof(diskint<OFF_T>));
    *index_mmap->getptr<diskint<OFF_T>>(newroot_offset) = 0;

    delete_from_memtree(type, addr, size);

    MemoryPayload memp;
    memp.type = type;
    memp.lo = addr;
    memp.hi = addr + (size - 1);
    memp.raw = false;
    memp.contents = newroot_offset;
    memp.trace_file_firstline = prev_lineno;
    memroot = memtree->insert(memroot, memp);

    return newroot_offset;
}

void Index::update_memtree_from_read(char type, Addr addr, size_t size,
                                     unsigned long long contents)
{
    auto data_ptr = make_unique<unsigned char[]>(size);
    unsigned char *data = data_ptr.get();
    if (bigend) {
        for (size_t i = 0; i < size; i++)
            data[i] = contents >> (8 * (size - 1 - i));
    } else {
        for (size_t i = 0; i < size; i++)
            data[i] = contents >> (8 * i);
    }

    MemoryPayload memp_search, memp;
    memp_search.type = type;
    memp_search.lo = addr;
    memp_search.hi = addr + (size - 1);
    memp_search.trace_file_firstline = prev_lineno;

    while (memp_search.lo <= memp_search.hi &&
           memtree->find_leftmost(memroot, memp_search, &memp, nullptr)) {
        if (memp.raw) {
            /*
             * FIXME: we could add a consistency check here. We're
             * seeing data being read from memory, and we thought we
             * already knew what was in that memory. If the data isn't
             * the same this time, what should we do? We could give a
             * warning at indexing time, for example. Or we could mark
             * the memory as indeterminate between the two reads (on
             * the basis that we don't know when it changed).
             *
             * For the moment, we simply ignore it. But if that
             * changed in future, this would be where to add code.
             */
        } else {
            diskint<OFF_T> *subroot =
                index_mmap->getptr<diskint<OFF_T>>(memp.contents);

            MemorySubPayload msp;
            msp.lo = memp_search.lo;
            msp.hi = min(memp.hi, memp_search.hi);

            while (msp.lo <= msp.hi) {
                MemorySubPayload msp_found;
                if (!memsubtree->find_leftmost(*subroot, msp, &msp_found,
                                               nullptr)) {
                    msp_found.lo = msp.hi + 1;
                    msp_found.hi = msp.hi;
                } else {
                    /*
                     * FIXME: similarly to above, we could consistency-check
                     * the data read from memory _now_ with what was already
                     * in our tree.
                     */
                }
                if (msp.lo < msp_found.lo) {
                    MemorySubPayload msp_insert;
                    msp_insert.lo = msp.lo;
                    msp_insert.hi = msp_found.lo - 1;
                    OFF_T contents_offset =
                        index_mmap->alloc(msp_insert.hi - msp_insert.lo + 1);
                    // Take account of alloc() perhaps having
                    // re-mmapped the file
                    subroot = index_mmap->getptr<diskint<OFF_T>>(memp.contents);
                    memcpy(index_mmap->getptr<unsigned char>(contents_offset),
                           data + (msp.lo - addr),
                           msp_insert.hi - msp_insert.lo + 1);
                    msp_insert.contents = contents_offset;

                    OFF_T new_subroot_value =
                        memsubtree->insert(*subroot, msp_insert);
                    // Take account of insert() perhaps having
                    // re-mmapped the file
                    subroot = index_mmap->getptr<diskint<OFF_T>>(memp.contents);
                    *subroot = new_subroot_value;
                }
                msp.lo = msp_found.hi + 1;
            }
        }
        memp_search.lo = memp.hi + 1;
        if (memp_search.lo == 0)
            break; // special case: address space wraparound!
    }
}

bool Index::read_memtree_value(char type, Addr addr, size_t size,
                               unsigned long long *output)
{
    unsigned char data[8], def[8];

    MemoryPayload memp_search;
    memp_search.type = type;
    memp_search.lo = addr;
    memp_search.hi = addr + (size - 1);

    memset(def, 0, size);

    while (memp_search.lo <= memp_search.hi) {
        bool found;
        MemoryPayload memp_got;
        found = memtree->find_leftmost(last_memroot, memp_search, &memp_got,
                                       nullptr);
        if (!found)
            break;

        Addr addr_lo = max(memp_search.lo, memp_got.lo);
        Addr addr_hi = min(memp_search.hi, memp_got.hi);

        if (memp_got.raw) {
            const unsigned char *treedata =
                index_mmap->getptr<unsigned char>(memp_got.contents);
            memcpy((char *)data + (addr_lo - addr),
                   treedata + (addr_lo - memp_got.lo), addr_hi - addr_lo + 1);
            memset((char *)def + (addr_lo - addr), 1, addr_hi - addr_lo + 1);
        } else {
            OFF_T subroot =
                *index_mmap->getptr<diskint<OFF_T>>(memp_got.contents);
            MemorySubPayload msp, msp_found;
            msp.lo = addr_lo;
            msp.hi = addr_hi;
            while (msp.lo <= msp.hi && memsubtree->find_leftmost(
                                           subroot, msp, &msp_found, nullptr)) {
                Addr subaddr_lo = max(msp.lo, msp_found.lo);
                Addr subaddr_hi = min(msp.hi, msp_found.hi);
                const unsigned char *treedata =
                    index_mmap->getptr<unsigned char>(msp_found.contents);
                memcpy((char *)data + (subaddr_lo - addr),
                       treedata + (subaddr_lo - msp_found.lo),
                       subaddr_hi - subaddr_lo + 1);
                memset((char *)def + (subaddr_lo - addr), 1,
                       subaddr_hi - subaddr_lo + 1);
                msp.lo = subaddr_hi + 1;
            }
        }

        memp_search.lo = memp_got.hi + 1;
        if (memp_search.lo == 0) // special case: address space wraparound
            break;
    }

    if (memchr(def, '\0', size))
        return false; // not every byte was available

    if (output) {
        unsigned long long outval = 0;
        if (type == 'm' && bigend) {
            for (size_t i = 0; i < size; i++)
                outval = (outval << 8) | data[i];
        } else {
            for (size_t i = size; i-- > 0;)
                outval = (outval << 8) | data[i];
        }
        *output = outval;
    }
    return true;
}

bool Index::read_memtree_reg(const RegisterId &reg, unsigned long long *output)
{
    return read_memtree_value('r', reg_offset(reg, curr_iflags), reg_size(reg),
                              output);
}

class CallDepthCountingTreeWalker {
    int curr_depth;
    const set<CallReturn> &callrets;
    set<CallReturn>::const_iterator it;

  public:
    CallDepthCountingTreeWalker(const set<CallReturn> &callrets)
        : curr_depth(0), callrets(callrets), it(callrets.begin())
    {
    }
    CallDepthCountingTreeWalker(const CallDepthCountingTreeWalker &) = delete;

    void operator()(SeqOrderPayload &main, SeqOrderAnnotation &, OFF_T,
                    SeqOrderAnnotation *, OFF_T, SeqOrderAnnotation *, OFF_T)
    {
        if (it != callrets.end() && it->line == main.trace_file_firstline) {
            curr_depth += it->direction;
            ++it;
        }
        main.call_depth = curr_depth;
    }
};

class CallDepthArrayTreeWalker {
    MMapFile *index_mmap;

  public:
    CallDepthArrayTreeWalker(MMapFile *index_mmap) : index_mmap(index_mmap) {}
    CallDepthArrayTreeWalker(const CallDepthArrayTreeWalker &) = delete;

    void operator()(SeqOrderPayload &mainpayload, SeqOrderAnnotation &main,
                    OFF_T, SeqOrderAnnotation *lc, OFF_T,
                    SeqOrderAnnotation *rc, OFF_T)
    {
        CallDepthArrayEntry current_node_array[2];
        CallDepthArrayEntry *arrays[3];
        int len[3], index[3];
        enum { ROOT, LC, RC, NARRAYS };

        // Fake up a tiny CallDepthArray describing the node we're
        // currently visiting, and also including a sentinel node
        // giving the total subtree size.
        current_node_array[0].call_depth = mainpayload.call_depth;
        current_node_array[0].cumulative_lines = 0;
        current_node_array[0].cumulative_insns = 0;
        current_node_array[1].call_depth = SENTINEL_DEPTH;
        current_node_array[1].cumulative_lines = mainpayload.trace_file_lines;
        current_node_array[1].cumulative_insns = 1;
        arrays[ROOT] = current_node_array;
        len[ROOT] = 2;

        // Set up to iterate over the CallDepthArrays for the left and
        // right subtrees, if present.
        if (lc) {
            arrays[LC] =
                index_mmap->getptr<CallDepthArrayEntry>(lc->call_depth_array);
            len[LC] = lc->call_depth_arraylen;
        } else {
            arrays[LC] = nullptr;
            len[LC] = 0;
        }
        if (rc) {
            arrays[RC] =
                index_mmap->getptr<CallDepthArrayEntry>(rc->call_depth_array);
            len[RC] = rc->call_depth_arraylen;
        } else {
            arrays[RC] = nullptr;
            len[RC] = 0;
        }

        // Do a preliminary merge pass over our collection of arrays
        // to figure out how big the new node's array will have to be,
        // by counting up the number of distinct call depths
        // represented in it.
        unsigned new_arraylen = 0;
        for (int i = 0; i < NARRAYS; i++)
            index[i] = 0;
        while (true) {
            unsigned next_depth = UINT_MAX;
            for (int i = 0; i < NARRAYS; i++)
                if (index[i] < len[i])
                    next_depth = min(next_depth,
                                     (unsigned)arrays[i][index[i]].call_depth);
            if (next_depth == UINT_MAX)
                break; // all arrays finished

            new_arraylen++;

            for (int i = 0; i < NARRAYS; i++)
                if (index[i] < len[i] &&
                    next_depth == arrays[i][index[i]].call_depth)
                    index[i]++;
        }

        main.call_depth_array =
            index_mmap->alloc(new_arraylen * sizeof(CallDepthArrayEntry));
        CallDepthArrayEntry *new_array =
            index_mmap->getptr<CallDepthArrayEntry>(main.call_depth_array);
        main.call_depth_arraylen = new_arraylen;

        // Reinitialise arrays[], since it contains pointers into the
        // memory-mapped file which might have been invalidated by the
        // above call to index_mmap->alloc.
        if (lc)
            arrays[LC] =
                index_mmap->getptr<CallDepthArrayEntry>(lc->call_depth_array);
        if (rc)
            arrays[RC] =
                index_mmap->getptr<CallDepthArrayEntry>(rc->call_depth_array);

        // Now do a second merge pass over the same arrays actually
        // populating the new array.
        unsigned new_arraypos = 0;
        unsigned clines = 0, cinsns = 0;
        for (int i = 0; i < NARRAYS; i++)
            index[i] = 0;
        while (true) {
            unsigned next_depth = UINT_MAX;
            for (int i = 0; i < NARRAYS; i++)
                if (index[i] < len[i])
                    next_depth = min(next_depth,
                                     (unsigned)arrays[i][index[i]].call_depth);
            if (next_depth == UINT_MAX)
                break; // all arrays finished

            assert(new_arraypos < new_arraylen);
            new_array[new_arraypos].call_depth = next_depth;
            new_array[new_arraypos].cumulative_lines = clines;
            new_array[new_arraypos].cumulative_insns = cinsns;
            new_array[new_arraypos].leftlink = index[LC];
            new_array[new_arraypos].rightlink = index[RC];
            new_arraypos++;

            for (int i = 0; i < NARRAYS; i++)
                if (index[i] < len[i] &&
                    next_depth == arrays[i][index[i]].call_depth) {
                    if (index[i] + 1 < len[i]) {
                        clines += (arrays[i][index[i] + 1].cumulative_lines -
                                   arrays[i][index[i]].cumulative_lines);
                        cinsns += (arrays[i][index[i] + 1].cumulative_insns -
                                   arrays[i][index[i]].cumulative_insns);
                    }
                    index[i]++;
                }
        }
        assert(new_arraypos == new_arraylen);
    }
};

void Index::got_event_common(TarmacEvent *event, bool is_instruction)
{
    /*
     * Tarmac files have been known to include chronological disorder,
     * e.g. a Cortex-M3 Fast Model might output 'CADI
     * E_simulation_stopped' with a time stamp _before_ that of the
     * preceding line containing a simulation-terminating semihosting
     * call instruction. To protect the rest of the code, we pretend
     * every line's timestamp is at least that of the previous line,
     * for indexing purposes.
     */
    Time ev_time = current_time;
    if (event && (ev_time == -(Time)1 || event->time > ev_time))
        ev_time = event->time;

    if (!seen_any_event)
        lineno_offset = true_lineno - lineno;

    if (!event || ev_time != current_time ||
        (seen_instruction_at_current_time && is_instruction)) {
        if (seen_any_event && linepos != oldpos) {
            SeqOrderPayload seqp;
            seqp.mod_time = current_time;
            seqp.pc = curr_pc;
            seqp.trace_file_pos = oldpos;
            seqp.trace_file_len = linepos - oldpos;
            seqp.trace_file_firstline = prev_lineno;
            seqp.trace_file_lines = lineno - prev_lineno;
            seqp.memory_root = memroot;
            seqp.call_depth = 0; // fill this in later
            seqroot = seqtree->insert(seqroot, seqp);

            if (curr_pc != KNOWN_INVALID_PC) {
                ByPCPayload bypcp;
                bypcp.trace_file_firstline = prev_lineno;
                bypcp.pc = curr_pc & ~(unsigned long long)1;
                bypcroot = bypctree->insert(bypcroot, bypcp);
            }
        }

        last_memroot = memroot;
        last_sp = curr_sp;
        memtree->commit();

        if (!event)
            return;

        if (current_time != ev_time) {
            current_time = ev_time;
            seen_instruction_at_current_time = false;
        }
        curr_pc = KNOWN_INVALID_PC;
        oldpos = linepos;

        prev_lineno = lineno;
        seen_any_event = true;
    }

    if (is_instruction)
        seen_instruction_at_current_time = true;
}

bool Index::parse_warning(const string &msg)
{
    reporter->indexing_warning(tarmac_filename, lineno + lineno_offset, msg);
    return false;
}

void Index::parse_tarmac_file(string tarmac_filename_)
{
    string line;

    tarmac_filename = tarmac_filename_;

    remove(index_filename.c_str());
    index_mmap = new MMapFile(index_filename, true);
    MagicNumber &magic = *index_mmap->newptr<MagicNumber>();

    OFF_T off_header = index_mmap->alloc(sizeof(FileHeader));
    {
        FileHeader &hdr = *index_mmap->getptr<FileHeader>(off_header);
        hdr.flags = 0;        // ensure FLAG_COMPLETE is not initially set
    }

    magic.setup();

    memtree = new AVLDisk<MemoryPayload, MemoryAnnotation>(*index_mmap);
    memsubtree = new AVLDisk<MemorySubPayload>(*index_mmap);
    seqtree = new AVLDisk<SeqOrderPayload, SeqOrderAnnotation>(*index_mmap);
    bypctree = new AVLDisk<ByPCPayload>(*index_mmap);
    memroot = seqroot = 0;
    // Set the initial contents of memory to be a sub-memtree, so that
    // we can fill in anything we later find out about via MR events.
    //
    // I cheat slightly here by setting the size parameter to 0,
    // really meaning the full size of the address space, because I
    // know that make_sub_memtree will subtract 1 from it and wrap
    // around.
    current_time = -(Time)1;
    seen_instruction_at_current_time = false;
    prev_lineno = 0; // used to fill in last-mod time in make_sub_memtree
    make_sub_memtree('m', 0, 0);
    last_memroot = memroot;
    bypcroot = 0;

    /*
     * Read in the input.
     */
    ifstream in(tarmac_filename.c_str(),
                std::ios_base::in | std::ios_base::binary);
    true_lineno = 0;
    lineno = 1;
    oldpos = 0;
    lineno_offset = 0;
    seen_any_event = false;
    prev_lineno = lineno;
    curr_pc = KNOWN_INVALID_PC;

    in.seekg(0, ios::end);
    reporter->indexing_start(in.tellg());
    in.seekg(0);

    TarmacLineParser parser(bigend, *this);

    while (true) {
        true_lineno++;
        if (seen_any_event)
            lineno++;

        if (in.eof()) {
            // If getline() above returned a truncated line, then it
            // will have set the fail flag on the stream, which will
            // have made in.tellg return -1. So clear the error flags,
            // find out the final file position, and leave this loop
            // before trying to read anything else.
            in.clear();
            linepos = in.tellg();
            break;
        }

        // Maintain linepos ourselves, rather than calling in.tellg() numerous
        // times. tellg() is a somehow slow function on some platforms, and this
        // alone allows a 2x speedup in parsing time.
        linepos += true_lineno > 1 ? line.size() + 1 : 0;

        reporter->indexing_progress(linepos);

        if (!getline(in, line))
            break;

        try {
            parser.parse(line);
        } catch (TarmacParseError e) {
            if (in.eof()) {
                ostringstream oss;
                oss << e.msg << endl << "tarmac-browser: ignoring parse error "
                    "on partial last line (trace truncated?)";
                reporter->indexing_warning(tarmac_filename, lineno, oss.str());
                break;
            } else {
                remove(index_filename.c_str());
                reporter->indexing_error(tarmac_filename, lineno, e.msg);
            }
        }
    }

    reporter->indexing_done();

    // Call got_event with no actual event, signalling end-of-file, so
    // that the last record is output (the same flushing of
    // accumulated data we'd do on seeing the next instruction, except
    // that then we stop without processing an instruction).
    got_event_common(nullptr, false);

    /*
     * Now we've got to the end of the trace and matched up calls to
     * returns as best we can within our own memory, postprocess the
     * main seqtree to fill in the call depth fields.
     */
    {
        CallDepthCountingTreeWalker visitor(found_callrets);
        seqtree->walk(seqroot, WalkOrder::Inorder, ref(visitor));
    }
    {
        CallDepthArrayTreeWalker visitor(index_mmap);
        seqtree->walk(seqroot, WalkOrder::Postorder, ref(visitor));
    }

    FileHeader &hdr = *index_mmap->getptr<FileHeader>(off_header);
    {
        unsigned flags = 0;
        if (bigend)
            flags |= FLAG_BIGEND;
        if (aarch64_used)
            flags |= FLAG_AARCH64_USED;
        flags |= FLAG_COMPLETE;
        hdr.flags = flags;
    }
    hdr.seqroot = seqroot;
    hdr.bypcroot = bypcroot;
    hdr.lineno_offset = lineno_offset;
}

IndexHeaderState check_index_header(const string &index_filename)
{
    MMapFile mmf(index_filename, false);

    MagicNumber &magic = *mmf.getptr<MagicNumber>(0);
    if (!magic.check())
        return IndexHeaderState::WrongMagic;

    FileHeader &hdr = *mmf.getptr<FileHeader>(sizeof(MagicNumber));
    if (!(hdr.flags & FLAG_COMPLETE))
        return IndexHeaderState::Incomplete;

    return IndexHeaderState::OK;
}

void run_indexer(const TracePair &trace, bool bigend)
{
    Index index(trace.index_filename, bigend);
    index.parse_tarmac_file(trace.tarmac_filename);
}

IndexReader::IndexReader(const TracePair &trace)
    : index_filename(trace.index_filename),
      tarmac_filename(trace.tarmac_filename), mmf(index_filename, false),
      tarmac(tarmac_filename, std::ios_base::in | std::ios_base::binary),
      bigend(), aarch64_used(), memtree(mmf), memsubtree(mmf), seqtree(mmf),
      bypctree(mmf)
{
    MagicNumber &magic = *mmf.getptr<MagicNumber>(0);
    if (!magic.check())
        reporter->errx(1, "%s: magic number did not match",
                       index_filename.c_str());
    FileHeader &hdr = *mmf.getptr<FileHeader>(sizeof(MagicNumber));
    seqroot = hdr.seqroot;
    bypcroot = hdr.bypcroot;
    bigend = (hdr.flags & FLAG_BIGEND);
    aarch64_used = (hdr.flags & FLAG_AARCH64_USED);
    lineno_offset = hdr.lineno_offset;
}

string IndexReader::read_tarmac(OFF_T pos, OFF_T len) const
{
    vector<char> vbuf(len);
    tarmac.seekg(pos);
    tarmac.read(&vbuf[0], len);
    return string(&vbuf[0], len);
}

vector<string> IndexReader::get_trace_lines(const SeqOrderPayload &node) const
{
    string sbuf = read_tarmac(node.trace_file_pos, node.trace_file_len);
    vector<string> lines;

    for (size_t pos = 0, size = sbuf.size(); pos < size;) {
        size_t nl = sbuf.find('\n', pos);
        if (nl == string::npos) {
            /*
             * If this is the end of a trace file with a truncated
             * final line, pretend there's a \n at the end of sbuf.
             * Then the next time we come round this loop we'll exit
             * because pos will exceed sbuf's length.
             */
            nl = sbuf.size();
        }

        string line = sbuf.substr(pos, nl - pos);
        if (line.size() > 0 && line[line.size() - 1] == '\r')
            line.resize(line.size() - 1);
        lines.push_back(line);

        pos = nl + 1;
    }

    return lines;
}

string IndexReader::get_trace_line(const SeqOrderPayload &node,
                                   unsigned lineno) const
{
    vector<string> lines = get_trace_lines(node);
    if (lineno >= lines.size())
        return "";
    return lines[lineno];
}

bool IndexNavigator::lookup_symbol(const string &name, uint64_t &addr,
                                   size_t &size) const
{
    if (!image)
        return false;

    if (const Symbol *sym = image->find_symbol(name)) {
        addr = sym->addr;
        size = sym->size;

        /*
         * FIXME: if the program under test is position-independent,
         * then it may have been loaded somewhere other than the base
         * address in the ELF file. It would be useful to have an
         * option to account for that here, by adjusting the returned
         * address.
         */
        return true;
    }

    return false;
}

bool IndexNavigator::lookup_symbol(const string &name, uint64_t &addr) const
{
    size_t size;
    return lookup_symbol(name, addr, size);
    ;
}

string IndexNavigator::get_symbolic_address(Addr addr, bool fallback) const
{
    ostringstream res;

    /*
     * FIXME: if the program under test is position-independent, then
     * it may have been loaded somewhere other than the base address
     * in the ELF file. It would be useful to have an option to
     * account for that here, by adjusting the address passed to
     * image->find_symbol.
     */

    const Symbol *sym = image ? image->find_symbol(addr) : nullptr;
    if (sym) {
        res << sym->getName();
        addr -= sym->addr;
        if (addr)
            res << " + " << hex << showbase << addr;
    } else if (fallback) {
        res << hex << showbase << addr;
    }
    return res.str();
}

struct IndexLRTSearcher {
    const IndexReader &index;
    unsigned target;
    unsigned mindepth_i, maxdepth_i;
    unsigned mindepth_o, maxdepth_o;

    unsigned minindex_i, maxindex_i;
    unsigned minindex_o, maxindex_o;
    // Identify the tree node (if any) whose call depth array minindex
    // and maxindex apply to.
    OFF_T curr;

    unsigned output_lines;

    class OutOfRangeException : exception {
    };

    IndexLRTSearcher(const IndexReader &index, unsigned target, unsigned mindepth_i,
                     unsigned maxdepth_i, unsigned mindepth_o,
                     unsigned maxdepth_o)
        : index(index), target(target), mindepth_i(mindepth_i),
          maxdepth_i(maxdepth_i), mindepth_o(mindepth_o),
          maxdepth_o(maxdepth_o), curr(-(OFF_T)1), output_lines(0)
    {
    }

    IndexLRTSearcher(const IndexLRTSearcher &) = delete;

    const CallDepthArrayEntry *lookup_array(const SeqOrderAnnotation *annot,
                                            unsigned idx)
    {
        auto *array = (const CallDepthArrayEntry *)index.index_offset(
            annot->call_depth_array);
        return array + idx;
    }

    unsigned find_depth(const SeqOrderAnnotation *annot, unsigned depth)
    {
        unsigned lo = 0, hi = annot->call_depth_arraylen;
        while (hi > lo) {
            unsigned mid =
                lo + (hi - lo) / 2; // might equal lo; never equals hi
            auto *entry = lookup_array(annot, mid);
            if (entry->call_depth >= depth)
                hi = mid;
            else
                lo = mid + 1;
        }

        // Correction: never return an array index beyond the end of
        // the array. The last entry should always be the terminating
        // sentinel node, so it's always safe to return a pointer to
        // it. This case will only come up if UINT_MAX was passed as
        // the input depth, which is greater than the SENTINEL_DEPTH
        // value used in the sentinel node itself.
        if (lo >= annot->call_depth_arraylen)
            lo = annot->call_depth_arraylen - 1;

        return lo;
    }

    int operator()(OFF_T lhs_off, const SeqOrderAnnotation *lhs, OFF_T here_off,
                   const SeqOrderPayload &here_p,
                   const SeqOrderAnnotation &here_a, OFF_T rhs_off,
                   const SeqOrderAnnotation *rhs)
    {
        if (curr != here_off) {
            curr = here_off;
            minindex_i = find_depth(&here_a, mindepth_i);
            maxindex_i = find_depth(&here_a, maxdepth_i);
            minindex_o = find_depth(&here_a, mindepth_o);
            maxindex_o = find_depth(&here_a, maxdepth_o);
        }

        if (lhs) {
            unsigned minindex_i_lhs =
                lookup_array(&here_a, minindex_i)->leftlink;
            unsigned maxindex_i_lhs =
                lookup_array(&here_a, maxindex_i)->leftlink;
            unsigned minindex_o_lhs =
                lookup_array(&here_a, minindex_o)->leftlink;
            unsigned maxindex_o_lhs =
                lookup_array(&here_a, maxindex_o)->leftlink;
            unsigned lines_i =
                (lookup_array(lhs, maxindex_i_lhs)->cumulative_lines -
                 lookup_array(lhs, minindex_i_lhs)->cumulative_lines);
            if (target < lines_i) {
                curr = lhs_off;
                minindex_i = minindex_i_lhs;
                maxindex_i = maxindex_i_lhs;
                minindex_o = minindex_o_lhs;
                maxindex_o = maxindex_o_lhs;
                return -1;
            }
            target -= lines_i;
            output_lines +=
                (lookup_array(lhs, maxindex_o_lhs)->cumulative_lines -
                 lookup_array(lhs, minindex_o_lhs)->cumulative_lines);
        }

        if (here_p.call_depth >= mindepth_i && here_p.call_depth < maxdepth_i) {
            if (target < here_p.trace_file_lines ||
                (target == here_p.trace_file_lines && !rhs)) {
                if (here_p.call_depth >= mindepth_o &&
                    here_p.call_depth < maxdepth_o) {
                    output_lines += target;
                }
                return 0;
            }
            target -= here_p.trace_file_lines;
        }
        if (here_p.call_depth >= mindepth_o && here_p.call_depth < maxdepth_o) {
            output_lines += here_p.trace_file_lines;
        }

        if (rhs) {
            unsigned minindex_i_rhs =
                lookup_array(&here_a, minindex_i)->rightlink;
            unsigned maxindex_i_rhs =
                lookup_array(&here_a, maxindex_i)->rightlink;
            unsigned minindex_o_rhs =
                lookup_array(&here_a, minindex_o)->rightlink;
            unsigned maxindex_o_rhs =
                lookup_array(&here_a, maxindex_o)->rightlink;
            unsigned lines =
                (lookup_array(rhs, maxindex_i_rhs)->cumulative_lines -
                 lookup_array(rhs, minindex_i_rhs)->cumulative_lines);
            if (target <= lines) {
                curr = rhs_off;
                minindex_i = minindex_i_rhs;
                maxindex_i = maxindex_i_rhs;
                minindex_o = minindex_o_rhs;
                maxindex_o = maxindex_o_rhs;
                return +1;
            }
            target -= lines;
            output_lines +=
                (lookup_array(rhs, maxindex_o_rhs)->cumulative_lines -
                 lookup_array(rhs, minindex_o_rhs)->cumulative_lines);
        }

        // If we get here, we were asked for an offset in the tree
        // that's completely out of bounds.
        throw OutOfRangeException();
    }
};

bool IndexNavigator::getmem_next(OFF_T memroot, char type, Addr addr,
                                 size_t size, const void **outdata,
                                 Addr *outaddr, size_t *outsize,
                                 unsigned *outline) const
{
    MemoryPayload memp_search;
    memp_search.type = type;
    memp_search.lo = addr;
    memp_search.hi = addr + (size - 1);

    while (memp_search.lo <= memp_search.hi) {
        bool found;
        MemoryPayload memp_got;
        found = index.memtree.find_leftmost(memroot, memp_search, &memp_got,
                                            nullptr);
        if (!found)
            return false;

        Addr addr_lo = max(memp_search.lo, memp_got.lo);
        Addr addr_hi = min(memp_search.hi, memp_got.hi);
        assert(addr_lo <= addr_hi);

        if (memp_got.raw) {
            size_t size = addr_hi - addr_lo + 1;
            const char *treedata =
                (const char *)index.index_offset(memp_got.contents);
            if (outdata)
                *outdata = treedata + (addr_lo - memp_got.lo);
            if (outaddr)
                *outaddr = addr_lo;
            if (outsize)
                *outsize = size;
            if (outline)
                *outline = memp_got.trace_file_firstline;
            return true;
        } else {
            OFF_T subroot = index.index_subtree_root(memp_got.contents);
            MemorySubPayload msp, msp_found;
            msp.lo = addr_lo;
            msp.hi = addr_hi;
            if (!index.memsubtree.find_leftmost(subroot, msp, &msp_found,
                                                nullptr)) {
                // Nothing in this subtree covers the range in
                // question, so it was a dead end. Go back to the main
                // memory tree and try the next thing in it.
                memp_search.lo = memp_got.hi + 1;
                if (memp_search.lo == 0)
                    return false; // address space wrapped round
                continue;
            }

            Addr subaddr_lo = max(msp.lo, msp_found.lo);
            Addr subaddr_hi = min(msp.hi, msp_found.hi);
            const char *treedata =
                (const char *)index.index_offset(msp_found.contents);
            size_t size = subaddr_hi - subaddr_lo + 1;

            if (outdata)
                *outdata = treedata + (subaddr_lo - msp_found.lo);
            if (outaddr)
                *outaddr = subaddr_lo;
            if (outsize)
                *outsize = size;
            if (outline)
                *outline = memp_got.trace_file_firstline;
            return true;
        }
    }

    return false;
}

unsigned IndexNavigator::getmem(OFF_T memroot, char type, Addr addr,
                                size_t size, void *outdata,
                                unsigned char *outdef) const
{
    unsigned retline = 0;
    MemoryPayload memp_search;
    memp_search.type = type;
    memp_search.lo = addr;
    memp_search.hi = addr + (size - 1);
    if (outdef)
        memset(outdef, 0, size);
    while (memp_search.lo <= memp_search.hi) {
        bool found;
        MemoryPayload memp_got;
        found = index.memtree.find_leftmost(memroot, memp_search, &memp_got,
                                            nullptr);
        if (!found)
            break;

        Addr addr_lo = max(memp_search.lo, memp_got.lo);
        Addr addr_hi = min(memp_search.hi, memp_got.hi);

        if (memp_got.raw) {
            const char *treedata =
                (const char *)index.index_offset(memp_got.contents);
            if (outdata)
                memcpy((char *)outdata + (addr_lo - addr),
                       treedata + (addr_lo - memp_got.lo),
                       addr_hi - addr_lo + 1);
            if (outdef)
                memset((char *)outdef + (addr_lo - addr), 1,
                       addr_hi - addr_lo + 1);
        } else {
            OFF_T subroot = index.index_subtree_root(memp_got.contents);
            MemorySubPayload msp, msp_found;
            msp.lo = addr_lo;
            msp.hi = addr_hi;
            while (msp.lo <= msp.hi && index.memsubtree.find_leftmost(
                                           subroot, msp, &msp_found, nullptr)) {
                Addr subaddr_lo = max(msp.lo, msp_found.lo);
                Addr subaddr_hi = min(msp.hi, msp_found.hi);
                const char *treedata =
                    (const char *)index.index_offset(msp_found.contents);
                if (outdata)
                    memcpy((char *)outdata + (subaddr_lo - addr),
                           treedata + (subaddr_lo - msp_found.lo),
                           subaddr_hi - subaddr_lo + 1);
                if (outdef)
                    memset((char *)outdef + (subaddr_lo - addr), 1,
                           subaddr_hi - subaddr_lo + 1);
                msp.lo = subaddr_hi + 1;
            }
        }

        if (retline < memp_got.trace_file_firstline)
            retline = memp_got.trace_file_firstline;

        memp_search.lo = memp_got.hi + 1;
        if (memp_search.lo == 0) // special case: address space wraparound
            break;
    }

    return retline;
}

bool IndexNavigator::get_reg_bytes(OFF_T memroot, const RegisterId &reg,
                                   vector<unsigned char> &val) const
{
    // Look up the iflags to pass to reg_offset, but only if they're
    // actually needed - because get_iflags recurses back to this
    // function, so we need at the very least to avoid looking up the
    // iflags in order to look up the iflags!
    Addr offset = reg_needs_iflags(reg) ? reg_offset(reg, get_iflags(memroot))
                                        : reg_offset(reg);

    size_t size = reg_size(reg);

    val.reserve(size);
    vector<unsigned char> def(size);

    getmem(memroot, 'r', offset, size, &val[0], &def[0]);

    for (auto c : def)
        if (!c)
            return false;
    return true;
}

pair<bool, uint64_t> IndexNavigator::get_reg_value(OFF_T memroot,
                                                   const RegisterId &reg) const
{
    size_t size = reg_size(reg);
    if (size > 8) {
        // Can't return an integer value for a register too large to
        // fit in a sensible C integer type. But we don't actually
        // fail an assertion here, because some code will want to call
        // this function indiscriminately for all registers in order
        // to opportunistically offer extra information when it
        // succeeds (e.g. translating register values as addresses).
        return make_pair(false, 0);
    }
    vector<unsigned char> val(size);
    bool is_defined = get_reg_bytes(memroot, reg, val);
    uint64_t toret = 0;
    for (size_t j = 0; j < size && j < 8; j++)
        toret |= (uint64_t)((unsigned char)val[j]) << 8 * j;
    return make_pair(is_defined, toret);
}

unsigned IndexNavigator::get_iflags(OFF_T memroot) const
{
    RegisterId reg = {RegPrefix::internal_flags, 0};
    auto gr = get_reg_value(memroot, reg);
    // If even the iflags aren't defined (e.g. at the very start of
    // the file), then we fill in a default value.
    return gr.first ? gr.second : 0;
}

namespace {
class SeqTimeFinder {
    Time t;

  public:
    SeqTimeFinder(Time t) : t(t) {}
    int cmp(const SeqOrderPayload &rhs) const
    {
        return t < rhs.mod_time ? -1 : t > rhs.mod_time ? +1 : 0;
    }
};
} // namespace

bool IndexNavigator::node_at_time(Time t, SeqOrderPayload *node) const
{
    return index.seqtree.find_rightmost(index.seqroot, SeqTimeFinder(t), node,
                                        nullptr);
}

namespace {
class SeqLineFinder {
    unsigned line;

  public:
    SeqLineFinder(unsigned line) : line(line) {}
    int cmp(const SeqOrderPayload &rhs) const
    {
        return (line < rhs.trace_file_firstline
                    ? -1
                    : line >= (rhs.trace_file_firstline + rhs.trace_file_lines)
                          ? +1
                          : 0);
    }
};
} // namespace

bool IndexNavigator::node_at_line(unsigned line, SeqOrderPayload *node) const
{
    return index.seqtree.find(index.seqroot, SeqLineFinder(line), node,
                              nullptr);
}

bool IndexNavigator::get_previous_node(SeqOrderPayload &in,
                                       SeqOrderPayload *out) const
{
    return index.seqtree.find(index.seqroot,
                              SeqLineFinder(in.trace_file_firstline - 1), out,
                              nullptr);
}

bool IndexNavigator::get_next_node(SeqOrderPayload &in,
                                   SeqOrderPayload *out) const
{
    return index.seqtree.find(
        index.seqroot,
        SeqLineFinder(in.trace_file_firstline + in.trace_file_lines), out,
        nullptr);
}

bool IndexNavigator::find_buffer_limit(bool end, SeqOrderPayload *node) const
{
    if (end)
        return index.seqtree.pred(index.seqroot, Infinity<SeqOrderPayload>(+1),
                                  node, nullptr);
    else
        return index.seqtree.succ(index.seqroot, Infinity<SeqOrderPayload>(-1),
                                  node, nullptr);
}

namespace {
struct RegMemChangesSearcher {
    // Input parameters for search
    unsigned minline;
    char type;
    Addr addr;
    int sign;

    // Helper stuff
    MemoryPayload key;
    bool use_key;
    int pass;

    // Result of search
    char result_type;
    Addr lo, hi;
    bool got_something, got_a_subtree;

    RegMemChangesSearcher(unsigned minline, char type, Addr addr, int sign)
        : minline(minline), type(type), addr(addr), sign(sign), pass(1),
          got_something(false)
    {
        key.type = type;
        key.lo = addr;
        key.hi = addr;
    }

    RegMemChangesSearcher(const RegMemChangesSearcher &) = delete;

    /*
     * Strategy: we may need two tree searches.
     *
     * Pass 1 down the tree: search for precisely the target address.
     * If we find a node containing it, go left of that node (if we
     * want a successor, or right if predecessor). Every time we go
     * left, record the node we came from as our best candidate so far
     * if it has the right mod time, or failing that, its right
     * subtree if that contains anything of the right mod time.
     *
     * After pass 1, we either have a node or a subtree listed as our
     * best candidate. If the latter, do a second search pass 2, in
     * which when we come to the node at the head of that subtree we
     * go right into it and then always as far left as we can manage,
     * to return the min thing in that subtree.
     */

    int operator()(OFF_T /*lhs_off*/, const MemoryAnnotation *lhs,
                   OFF_T /*here_off*/, const MemoryPayload &here_p,
                   const MemoryAnnotation & /*here_a*/, OFF_T /*rhs_off*/,
                   const MemoryAnnotation *rhs)
    {
        if (pass == 1) {
            int cmp = key.cmp(here_p);
            if (cmp == 0)
                cmp = -sign;
            if (cmp == -sign) {
                const MemoryAnnotation *subtree = sign > 0 ? rhs : lhs;

                if (type == here_p.type &&
                    here_p.trace_file_firstline >= minline) {
                    // The node 'here' is our best so far.
                    result_type = here_p.type;
                    lo = here_p.lo;
                    hi = here_p.hi;
                    got_something = true;
                    got_a_subtree = false;
                } else if (subtree && subtree->latest >= minline) {
                    // The subtree to the right (or left if sign<0) of
                    // this node contains something appropriate, and
                    // whatever that something is, that's our best so far.
                    result_type = here_p.type;
                    lo = here_p.lo;
                    hi = here_p.hi;
                    got_something = true;
                    got_a_subtree = true;
                }
            }
            return cmp;
        } else {
            assert(pass == 2);
            if (use_key) {
                int cmp = key.cmp(here_p);
                if (cmp == 0) {
                    use_key = false;
                    return sign;
                } else {
                    return cmp;
                }
            } else {
                /*
                 * Now we're in the subtree that pass 1 found, so we
                 * need to return the smallest (or largest, if sign<0)
                 * thing in it that has the right mod time. To do
                 * this, we check the left subtree, then the element
                 * itself, then the right. (Or vice versa, if sign<0.)
                 */
                const MemoryAnnotation *subtree = sign > 0 ? lhs : rhs;
                if (subtree && subtree->latest >= minline)
                    return -sign;
                if (here_p.trace_file_firstline >= minline) {
                    result_type = here_p.type;
                    lo = here_p.lo;
                    hi = here_p.hi;
                    got_something = true;
                    return 0;
                }
                /*
                 * Because we know from pass 1 that _something_ in
                 * this tree is acceptable, it's guaranteed that if
                 * neither of the above checks told us where to go,
                 * this will be the remaining possible place to look.
                 */
                return +sign;
            }
        }
    }

    bool need_second_pass()
    {
        if (got_something && got_a_subtree) {
            use_key = true;
            key.lo = lo;
            key.hi = hi;
            pass = 2;
            got_something = false;
            return true;
        } else {
            return false;
        }
    }

    bool get_result(Addr &lo_out, Addr &hi_out)
    {
        if (got_something && result_type == type) {
            lo_out = lo;
            hi_out = hi;
            return true;
        } else {
            return false;
        }
    }
};
} // namespace

bool IndexNavigator::find_next_mod(OFF_T memroot, char type, Addr addr,
                                   unsigned minline, int sign, Addr &lo,
                                   Addr &hi) const
{
    RegMemChangesSearcher rmcs(minline, type, addr, sign);
    index.memtree.search(memroot, ref(rmcs), nullptr);
    if (rmcs.need_second_pass())
        index.memtree.search(memroot, ref(rmcs), nullptr);
    return rmcs.get_result(lo, hi);
}

unsigned IndexNavigator::lrt_translate(unsigned line, unsigned mindepth_i,
                                       unsigned maxdepth_i, unsigned mindepth_o,
                                       unsigned maxdepth_o) const
{
    auto pair = lrt_translate_may_fail(line, mindepth_i, maxdepth_i, mindepth_o,
                                       maxdepth_o);
    assert(pair.first);
    return pair.second;
}

std::pair<bool, unsigned>
IndexNavigator::lrt_translate_may_fail(unsigned line, unsigned mindepth_i,
                                       unsigned maxdepth_i, unsigned mindepth_o,
                                       unsigned maxdepth_o) const
{
    IndexLRTSearcher searcher(index, line, mindepth_i, maxdepth_i, mindepth_o,
                              maxdepth_o);
    bool success;
    try {
        success = index.seqtree.search(index.seqroot, ref(searcher), nullptr);
    } catch (IndexLRTSearcher::OutOfRangeException) {
        success = false;
    }
    unsigned output = success ? searcher.output_lines : 0;
    return std::make_pair(success, output);
}

unsigned IndexNavigator::lrt_translate_range(
    unsigned linestart, unsigned lineend, unsigned mindepth_i,
    unsigned maxdepth_i, unsigned mindepth_o, unsigned maxdepth_o) const
{
    return (
        lrt_translate(lineend, mindepth_i, maxdepth_i, mindepth_o, maxdepth_o) -
        lrt_translate(linestart, mindepth_i, maxdepth_i, mindepth_o,
                      maxdepth_o));
}
