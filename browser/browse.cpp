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

#include "browse.hh"
#include "libtarmac/disktree.hh"
#include "libtarmac/expr.hh"
#include "libtarmac/index.hh"
#include "libtarmac/index_ds.hh"
#include "libtarmac/memtree.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/registers.hh"

#include <climits>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using std::exception;
using std::invalid_argument;
using std::istringstream;
using std::make_unique;
using std::max;
using std::min;
using std::ostringstream;
using std::ref;
using std::string;
using std::vector;

inline int FoldStatePayload::cmp(const FoldStatePayload &rhs) const
{
    return (last_physical_line < rhs.first_physical_line
                ? -1
                : first_physical_line > rhs.last_physical_line ? +1 : 0);
}

class FoldStateSearchOutOfRangeException : exception {
};

struct FoldStateByPhysLineSearcher {
    unsigned target, vislines_before;

    FoldStateByPhysLineSearcher(unsigned target)
        : target(target), vislines_before(0)
    {
    }

    FoldStateByPhysLineSearcher(const FoldStateByPhysLineSearcher &) = delete;

    int operator()(FoldStateAnnotation *lhs, FoldStatePayload &here,
                   FoldStateAnnotation *rhs)
    {
        if (lhs) {
            if (target < lhs->n_physical_lines)
                return -1;
            target -= lhs->n_physical_lines;
            vislines_before += lhs->n_visible_lines;
        }
        if (target < here.n_physical_lines ||
            (target == here.n_physical_lines && !rhs))
            return 0;
        target -= here.n_physical_lines;
        vislines_before += here.n_visible_lines;
        if (rhs) {
            if (target <= rhs->n_physical_lines)
                return +1;
            target -= rhs->n_physical_lines;
            vislines_before += rhs->n_visible_lines;
        }
        assert(target != 0);
        throw FoldStateSearchOutOfRangeException();
    }
};

struct FoldStateByVisLineSearcher {
    unsigned target, vislines_before, physlines_before;

    FoldStateByVisLineSearcher(unsigned target)
        : target(target), vislines_before(0), physlines_before(0)
    {
    }
    FoldStateByVisLineSearcher(const FoldStateByVisLineSearcher &) = delete;

    int operator()(FoldStateAnnotation *lhs, FoldStatePayload &here,
                   FoldStateAnnotation *rhs)
    {
        if (lhs) {
            if (target < lhs->n_visible_lines)
                return -1;
            target -= lhs->n_visible_lines;
            vislines_before += lhs->n_visible_lines;
            physlines_before += lhs->n_physical_lines;
        }
        if (target < here.n_visible_lines ||
            (target == here.n_visible_lines && !rhs))
            return 0;
        target -= here.n_visible_lines;
        vislines_before += here.n_visible_lines;
        physlines_before += here.n_physical_lines;
        if (rhs) {
            if (target <= rhs->n_visible_lines)
                return +1;
            target -= rhs->n_visible_lines;
            vislines_before += rhs->n_visible_lines;
            physlines_before += rhs->n_physical_lines;
        }
        assert(target != 0);
        throw FoldStateSearchOutOfRangeException();
    }
};

struct FoldStateEndOfListSearcher {
    unsigned vislines_before = 0;

    FoldStateEndOfListSearcher() = default;
    FoldStateEndOfListSearcher(const FoldStateEndOfListSearcher &) = delete;

    int operator()(FoldStateAnnotation *lhs, FoldStatePayload &here,
                   FoldStateAnnotation *rhs)
    {
        if (lhs)
            vislines_before += lhs->n_visible_lines;
        vislines_before += here.n_visible_lines;
        return +1;
    }
};

DecodedTraceLine::DecodedTraceLine(bool bigend, const string &line)
{
    TarmacLineParser parser(bigend, *this);
    try {
        parser.parse(line);
    } catch (TarmacParseError err) {
        // Ignore parse failures; we just leave the output event
        // fields set to null.
    }
}

void DecodedTraceLine::got_event(MemoryEvent &ev)
{
    mev = make_unique<MemoryEvent>(ev);
}

void DecodedTraceLine::got_event(RegisterEvent &ev)
{
    rev = make_unique<RegisterEvent>(ev);
}

void DecodedTraceLine::got_event(InstructionEvent &ev)
{
    iev = make_unique<InstructionEvent>(ev);
}

HighlightedLine::HighlightedLine(const string &text, size_t display_len)
    : text(text), display_len(display_len), disassembly_start(display_len),
      highlights(display_len, HL_NONE), iev(nullptr)
{
    // We don't need to bother getting endianness right for this
    // application
    TarmacLineParser parser(false, *this);
    try {
        parser.parse(text);
    } catch (TarmacParseError err) {
        // Ignore parse failures; we just leave the output event
        // fields set to null.
    }
}

HighlightedLine::HighlightedLine(const string &text)
    : HighlightedLine(text, text.size())
{
}

void HighlightedLine::highlight(size_t start, size_t end, HighlightClass hc)
{
    if (hc == HL_DISASSEMBLY && disassembly_start > start)
        disassembly_start = start;
    for (size_t i = start; i < end && i < highlights.size(); i++)
        highlights[i] = hc;
}

void HighlightedLine::got_event(InstructionEvent &ev)
{
    iev = make_unique<InstructionEvent>(ev);
}

void HighlightedLine::replace_instruction(Browser &br)
{
    if (!iev)
        return;

#define MATCH(mask, value) ((iev->instruction & (mask)) == (value))
#define PREFIX(prefix)                                                         \
    (iev->disassembly.substr(0, sizeof(prefix) - 1) == prefix)
#define BITS(start, size) ((iev->instruction >> (start)) & ((1U << (size)) - 1))
#define SEXT(value, size)                                                      \
    (((value) ^ (1U << ((size)-1))) - (1ULL << ((size)-1)))

    int which_operand = 0;
    uint64_t target;
    if (iev->iset == ARM && iev->width == 32) {
        if (((MATCH(0x0f000000, 0x0b000000) && PREFIX("BL")) ||
             (MATCH(0x0f000000, 0x0a000000) && PREFIX("B"))) &&
            !MATCH(0xf0000000, 0xf0000000)) {
            target = iev->pc + 8 + SEXT((BITS(0, 24) << 2), 26);
        } else if (MATCH(0xfe000000, 0xfa000000) && PREFIX("BLX")) {
            target =
                iev->pc + 8 + SEXT((BITS(0, 24) << 2) + (BITS(24, 1) << 1), 26);
            target |= 1;
        } else {
            return;
        }
    } else if (iev->iset == THUMB && iev->width == 16) {
        if (MATCH(0xf000, 0xd000) && !MATCH(0xfe00, 0xde00) && PREFIX("B")) {
            // conditional B
            target = iev->pc + 4 + SEXT((BITS(0, 8) << 1), 9);
            target |= 1;
        } else if (MATCH(0xf800, 0xe000) && PREFIX("B")) {
            // unconditional B
            target = iev->pc + 4 + SEXT((BITS(0, 11) << 1), 12);
            target |= 1;
        } else if ((MATCH(0xfd00, 0xb100) && PREFIX("CBZ")) ||
                   (MATCH(0xfd00, 0xb900) && PREFIX("CBNZ"))) {
            which_operand = 1;
            // positive offsets only, so no SEXT
            target = iev->pc + 4 + ((BITS(3, 5) << 1) + (BITS(9, 1) << 6));
            target |= 1;
        } else {
            return;
        }
    } else if (iev->iset == THUMB && iev->width == 32) {
        if ((MATCH(0xf800d000, 0xf000d000) && PREFIX("BL")) ||
            (MATCH(0xf800d000, 0xf0009000) && PREFIX("B"))) {
            // BL or unconditional B
            unsigned S = BITS(26, 1);
            target = iev->pc + 4 +
                     SEXT((BITS(0, 11) << 1) + (BITS(16, 10) << 12) +
                              ((BITS(11, 1) ^ !S) << 22) +
                              ((BITS(13, 1) ^ !S) << 23) + (S << 24),
                          25);
            target |= 1;
        } else if (MATCH(0xf800d000, 0xf0008000) &&
                   !MATCH(0xfb80d000, 0xf3808000) && PREFIX("B")) {
            // conditional B, with four fewer offset bits
            target = iev->pc + 4 +
                     SEXT((BITS(0, 11) << 1) + (BITS(16, 6) << 12) +
                              (BITS(13, 1) << 18) + (BITS(11, 1) << 19) +
                              (BITS(26, 1) << 20),
                          21);
            target |= 1;
        } else if (MATCH(0xf800d001, 0xf000c000) && PREFIX("BLX")) {
            unsigned S = BITS(26, 1);
            unsigned aligned_pc = iev->pc & ~3U;
            target = aligned_pc + 4 +
                     SEXT((BITS(1, 10) << 2) + (BITS(16, 10) << 12) +
                              ((BITS(11, 1) ^ !S) << 22) +
                              ((BITS(13, 1) ^ !S) << 23) + (S << 24),
                          25);
        } else {
            return;
        }
    } else if (iev->iset == A64 && iev->width == 32) {
        if ((MATCH(0xfc000000, 0x94000000) && PREFIX("BL")) ||
            (MATCH(0xfc000000, 0x14000000) && PREFIX("B"))) {
            target = iev->pc + SEXT((BITS(0, 26) << 2), 28);
        } else if (MATCH(0xff000010, 0x54000000) && PREFIX("B.")) {
            target = iev->pc + SEXT((BITS(5, 19) << 2), 21);
        } else if ((MATCH(0x7f000000, 0x34000000) && PREFIX("CBZ")) ||
                   (MATCH(0x7f000000, 0x35000000) && PREFIX("CBNZ"))) {
            which_operand = 1;
            target = iev->pc + SEXT((BITS(5, 19) << 2), 21);
        } else if ((MATCH(0x7f000000, 0x36000000) && PREFIX("TBZ")) ||
                   (MATCH(0x7f000000, 0x37000000) && PREFIX("TBNZ"))) {
            which_operand = 2;
            target = iev->pc + SEXT((BITS(5, 14) << 2), 16);
        } else {
            return;
        }
    }

    if (iev->iset != A64)
        target = (uint32_t)target;

#undef MATCH
#undef PREFIX
#undef BITS
#undef SEXT

    size_t operand_start = 0;
    while (operand_start < iev->disassembly.size() &&
           !isspace((unsigned char)iev->disassembly[operand_start]))
        operand_start++;
    while (operand_start < iev->disassembly.size() &&
           isspace((unsigned char)iev->disassembly[operand_start]))
        operand_start++;
    while (which_operand-- > 0) {
        while (operand_start < iev->disassembly.size() &&
               iev->disassembly[operand_start] != ',')
            operand_start++;
        if (operand_start < iev->disassembly.size())
            operand_start++;
        while (operand_start < iev->disassembly.size() &&
               isspace((unsigned char)iev->disassembly[operand_start]))
            operand_start++;
    }

    size_t operand_end = operand_start;
    while (operand_end < iev->disassembly.size() &&
           iev->disassembly[operand_end] != ',')
        operand_end++;

    string new_disassembly = iev->disassembly.substr(0, operand_start) +
                             br.get_symbolic_address(target, true) +
                             iev->disassembly.substr(operand_end);

    highlights.resize(disassembly_start);
    text = text.substr(0, disassembly_start) + new_disassembly;
    highlights.resize(text.size(), HL_DISASSEMBLY);
    display_len = text.size();
}

Browser::TraceView::TraceView(Browser &br) : br(br), index(br.index)
{
    // Populate fold_states with a single initial entry covering
    // the whole file.
    SeqOrderPayload last;
    if (br.find_buffer_limit(true, &last)) {
        set_fold_state(1, last.trace_file_firstline + last.trace_file_lines - 1,
                       0, UINT_MAX);
    }
}

void Browser::TraceView::set_fold_state(unsigned firstline, unsigned lastline,
                                        unsigned mindepth, unsigned maxdepth)
{
    FoldStatePayload fsp, fsp_found;
    fsp.first_physical_line = firstline;
    fsp.last_physical_line = lastline;
    fsp.mindepth = mindepth;
    fsp.maxdepth = maxdepth;
    fsp.n_physical_lines = lastline - firstline + 1;
    fsp.first_quasivis_line =
        br.lrt_translate(firstline - 1, 0, UINT_MAX, mindepth, maxdepth);
    fsp.n_visible_lines =
        (br.lrt_translate(lastline - 1 + 1, 0, UINT_MAX, mindepth, maxdepth) -
         fsp.first_quasivis_line);

    /*
     * Clear space for the new fsp in the tree, by deleting any
     * previous entry overlapping its space, and reinserting the
     * non-superseded parts if it only partly overlapped.
     */
    while (fold_states.remove(fsp, &fsp_found)) {
        if (fsp_found.first_physical_line < fsp.first_physical_line) {
            FoldStatePayload fsp_part = fsp_found;
            fsp_part.last_physical_line = fsp.first_physical_line - 1;
            fsp_part.n_physical_lines = (fsp_part.last_physical_line -
                                         fsp_part.first_physical_line + 1);
            fsp_part.n_visible_lines = br.lrt_translate_range(
                fsp_part.first_physical_line - 1, fsp_part.last_physical_line,
                0, UINT_MAX, fsp_part.mindepth, fsp_part.maxdepth);
            fold_states.insert(fsp_part);
        }
        if (fsp_found.last_physical_line > fsp.last_physical_line) {
            FoldStatePayload fsp_part = fsp_found;
            fsp_part.first_physical_line = fsp.last_physical_line + 1;
            fsp_part.n_physical_lines = (fsp_part.last_physical_line -
                                         fsp_part.first_physical_line + 1);
            unsigned first_quasivis_line_after =
                fsp_part.first_quasivis_line + fsp_part.n_visible_lines;
            fsp_part.n_visible_lines = br.lrt_translate_range(
                fsp_part.first_physical_line - 1, fsp_part.last_physical_line,
                0, UINT_MAX, fsp_part.mindepth, fsp_part.maxdepth);
            fsp_part.first_quasivis_line =
                first_quasivis_line_after - fsp_part.n_visible_lines;
            fold_states.insert(fsp_part);
        }
    }
    fold_states.insert(fsp);
}

unsigned Browser::TraceView::visible_to_physical_line(unsigned visline)
{
    FoldStatePayload fsp;

    /*
     * Find which fold_states range we're in.
     */
    FoldStateByVisLineSearcher searcher(visline);
    bool ret = fold_states.search(ref(searcher), &fsp);
    unsigned physline = 1 + searcher.physlines_before;
    if (ret)
        physline += br.lrt_translate_range(
            fsp.first_quasivis_line,
            fsp.first_quasivis_line + visline - searcher.vislines_before,
            fsp.mindepth, fsp.maxdepth, 0, UINT_MAX);
    return physline;
}

unsigned Browser::TraceView::physical_to_visible_line(unsigned physline)
{
    FoldStatePayload fsp;
    unsigned vislines_before;

    /*
     * Find which fold_states range we're in.
     */
    FoldStateByPhysLineSearcher searcher(physline - 1);
    bool ret = fold_states.search(ref(searcher), &fsp);
    vislines_before = searcher.vislines_before;
    if (ret)
        vislines_before +=
            br.lrt_translate_range(fsp.first_physical_line - 1, physline - 1, 0,
                                   UINT_MAX, fsp.mindepth, fsp.maxdepth);
    return vislines_before;
}

unsigned Browser::TraceView::total_visible_lines()
{
    FoldStateEndOfListSearcher searcher;
    bool ret = fold_states.search(ref(searcher), nullptr);
    return searcher.vislines_before;
}

bool Browser::get_node_by_physline(unsigned physline, SeqOrderPayload *node,
                                   unsigned *offset_within_node)
{
    bool ret = node_at_line(physline, node);
    if (ret && offset_within_node)
        *offset_within_node = physline - node->trace_file_firstline;
    return ret;
}

bool Browser::TraceView::get_node_by_visline(unsigned visline,
                                             SeqOrderPayload *node,
                                             unsigned *offset_within_node)
{
    return br.get_node_by_physline(visible_to_physical_line(visline), node,
                                   offset_within_node);
}

void Browser::TraceView::update_visible_node()
{
    // visline_of_next_node is the first visible node after this
    // one.
    unsigned visline_of_next_node =
        physical_to_visible_line(curr_logical_node.trace_file_firstline +
                                 curr_logical_node.trace_file_lines);
    if (visline_of_next_node >= 1) {
        unsigned physline_of_prev_node =
            visible_to_physical_line(visline_of_next_node - 1);
        bool ret = br.node_at_line(physline_of_prev_node, &curr_visible_node);
        assert(ret);
    } else {
        curr_visible_node = curr_logical_node;
    }
}

void Browser::TraceView::visible_to_logical_node(SeqOrderPayload &visnode,
                                                 SeqOrderPayload *lognode)
{
    unsigned curr_last_visline = physical_to_visible_line(
        visnode.trace_file_firstline + visnode.trace_file_lines - 1);
    unsigned physline_of_target_node =
        visible_to_physical_line(curr_last_visline + 1) - 1;
    bool ret = br.node_at_line(physline_of_target_node, lognode);
    assert(ret);
}

void Browser::TraceView::update_logical_node()
{
    visible_to_logical_node(curr_visible_node, &curr_logical_node);
}

bool Browser::TraceView::goto_time(Time t)
{
    if (!br.node_at_time(t, &curr_logical_node))
        return false;
    update_visible_node();
    return true;
}

bool Browser::TraceView::goto_physline(unsigned line)
{
    if (!br.node_at_line(line, &curr_logical_node))
        return false;
    update_visible_node();
    return true;
}

bool Browser::TraceView::goto_visline(unsigned line)
{
    bool ok = false;

    try {
        ok = get_node_by_visline(line, &curr_visible_node);
    } catch (FoldStateSearchOutOfRangeException) {
    }

    if (ok)
        update_logical_node();
    return ok;
}

bool Browser::TraceView::goto_buffer_limit(bool end)
{
    if (!br.find_buffer_limit(end, &curr_logical_node))
        return false;
    update_visible_node();
    return true;
}

bool Browser::TraceView::lookup_register(const string &name, uint64_t &out)
{
    RegisterId r;

    /*
     * Special cases.
     */
    if (name == "pc") {
        SeqOrderPayload next_logical_node;
        unsigned target_line = (curr_logical_node.trace_file_firstline +
                                curr_logical_node.trace_file_lines);
        bool got_next_node = br.node_at_line(target_line, &next_logical_node);
        if (got_next_node)
            out = next_logical_node.pc;
        return got_next_node;
    }

    if (name == "sp") {
        if (index.isAArch64())
            r = REG_64_xsp;
        else
            r = REG_32_sp;
        goto found;
    }

    if (name == "lr") {
        if (index.isAArch64())
            r = REG_64_xlr;
        else
            r = REG_32_lr;
        goto found;
    }

    if (lookup_reg_name(r, name))
        goto found;

    return false; // not found
found:

    /*
     * Now we've got the id of our register, we can look it up in
     * the current memtree.
     */
    vector<unsigned char> val(reg_size(r)), def(reg_size(r));
    auto memroot = curr_logical_node.memory_root;
    unsigned iflags = br.get_iflags(memroot);
    Addr roffset = reg_offset(r, iflags);
    size_t rsize = reg_size(r);
    br.getmem(memroot, 'r', roffset, rsize, &val[0], &def[0]);

    out = 0;
    for (size_t j = rsize; j-- > 0;) {
        if (!def[j])
            throw invalid_argument("register " + name + " is not defined");

        out = (out << 8) | val[j];
    }

    return true;
}

bool Browser::TraceView::goto_pc(unsigned long long pc, int dir)
{
    pc &= ~(unsigned long long)1;
    ByPCPayload pcfinder, pcfound;
    pcfinder.pc = pc;
    pcfinder.trace_file_firstline =
        (curr_logical_node.trace_file_firstline + (dir > 0 ? +1 : -1));
    bool ret = (dir > 0 ? index.bypctree.succ(index.bypcroot, pcfinder,
                                              &pcfound, nullptr)
                        : index.bypctree.pred(index.bypcroot, pcfinder,
                                              &pcfound, nullptr));
    if (!ret || pcfound.pc != pc)
        return false;

    /*
     * When we aim at a particular pc value, the above search will
     * find us a trace-event node which executes an instruction
     * _fetched from_ that location. But if we use its time stamp
     * for the goto_time call, that will leave the position just
     * _after_ that instruction, which isn't really what we want -
     * if I asked for the pc to be left at the entry point to a
     * function (for example), then I wanted to examine the
     * register state _before_ that function begins executing, not
     * after the first instruction of its prologue. (E.g. I very
     * likely wanted to look at the register and stack arguments
     * set up by the caller.)
     */
    unsigned target_line = pcfound.trace_file_firstline;
    if (target_line > 0)
        target_line--;
    return goto_physline(target_line);
}

struct ImageExecutionContext : ExecutionContext {
    Browser &br;
    ImageExecutionContext(Browser &br) : br(br) {}

    bool lookup(const string &name, Context context, uint64_t &out) const
    {
        if (context == ExecutionContext::Context::Symbol)
            return br.lookup_symbol(name, out);
        return false;
    }
};

struct TraceExecutionContext : ImageExecutionContext {
    Browser::TraceView &vu;
    TraceExecutionContext(Browser::TraceView &vu)
        : ImageExecutionContext(vu.br), vu(vu)
    {
    }

    bool lookup(const string &name, Context context, uint64_t &out) const
    {
        if (context == ExecutionContext::Context::Register)
            return vu.lookup_register(name, out);
        return ImageExecutionContext::lookup(name, context, out);
    }
};

Addr evaluate_expression_general(const string &line, const ExecutionContext &ec)
{
    ostringstream err;
    ExprPtr expr = parse_expression(line, err);

    if (!expr)
        throw invalid_argument(err.str());

    try {
        return expr->evaluate(ec);
    } catch (EvaluationError &ee) {
        throw invalid_argument(ee.msg);
    }
}

Addr evaluate_expression_plain(const string &line)
{
    TrivialExecutionContext ec;
    return evaluate_expression_general(line, ec);
}

Addr Browser::evaluate_expression_addr(const string &line)
{
    ImageExecutionContext ec(*this);
    return evaluate_expression_general(line, ec);
}

Addr Browser::TraceView::evaluate_expression_addr(const string &line)
{
    TraceExecutionContext ec(*this);
    return evaluate_expression_general(line, ec);
}

bool Browser::TraceView::position_hidden()
{
    SeqOrderPayload expected_logical_node;
    visible_to_logical_node(curr_visible_node, &expected_logical_node);
    return (expected_logical_node.mod_time != curr_logical_node.mod_time);
}

bool Browser::TraceView::get_current_pc(unsigned long long &pc)
{
    /*
     * When we're asked for the current PC value, we base it on the
     * immediate successor to the current logical node, i.e. the
     * instruction that we're showing the registers and memory just
     * _before_ the execution of.
     */
    SeqOrderPayload next_logical_node;
    unsigned target_line = (curr_logical_node.trace_file_firstline +
                            curr_logical_node.trace_file_lines);
    if (!br.node_at_line(target_line, &next_logical_node))
        return false;
    pc = next_logical_node.pc;
    return true;
}

bool Browser::TraceView::next_visible_node(SeqOrderPayload &node,
                                           SeqOrderPayload *ret)
{
    unsigned curr_last_visline = physical_to_visible_line(
        node.trace_file_firstline + node.trace_file_lines - 1);
    return get_node_by_visline(curr_last_visline + 1, ret);
}

bool Browser::TraceView::prev_visible_node(SeqOrderPayload &node,
                                           SeqOrderPayload *ret)
{
    unsigned curr_first_visline =
        physical_to_visible_line(node.trace_file_firstline);
    return (curr_first_visline > 1 &&
            get_node_by_visline(curr_first_visline - 1, ret));
}

bool Browser::TraceView::next_visible_node(SeqOrderPayload *ret)
{
    return next_visible_node(curr_visible_node, ret);
}

bool Browser::TraceView::prev_visible_node(SeqOrderPayload *ret)
{
    return prev_visible_node(curr_visible_node, ret);
}

bool Browser::TraceView::physline_range_for_containing_function(
    SeqOrderPayload &node, unsigned *firstline, unsigned *lastline,
    unsigned *depth)
{
    // First, find out the depth of the function in question.
    // Here we check the call depth of the visible nodes both
    // before and after the cursor underline; we'll hide the
    // _higher_ of these levels, so that hitting 'fold' either
    // just after a call _or_ just before a return folds the
    // callee in those control transfers rather than the
    // caller.
    SeqOrderPayload nextnode;
    bool got_nextnode = next_visible_node(node, &nextnode);

    unsigned fold_depth = node.call_depth;
    if (got_nextnode)
        fold_depth = max(fold_depth, (unsigned)nextnode.call_depth);

    if (fold_depth == 0)
        return false;

    // Now find the limits of the function at this depth, by searching
    // for the next/previous node at less than fold_depth.
    unsigned physlinehere = node.trace_file_firstline + node.trace_file_lines;

    unsigned foldedlineafter =
        br.lrt_translate(physlinehere - 1, 0, UINT_MAX, 0, fold_depth);
    unsigned physlineafter =
        br.lrt_translate(foldedlineafter, 0, fold_depth, 0, UINT_MAX) + 1;
    unsigned physlinefirstwithin =
        br.lrt_translate(foldedlineafter - 1, 0, fold_depth, 0, UINT_MAX) + 2;

    *firstline = physlinefirstwithin;
    *lastline = physlineafter - 1;
    *depth = fold_depth;
    return true;
}

bool Browser::TraceView::physline_range_for_folded_function_after(
    SeqOrderPayload &visnode, unsigned *firstline, unsigned *lastline,
    unsigned *depth)
{
    SeqOrderPayload lognode;
    visible_to_logical_node(visnode, &lognode);

    if (visnode.mod_time == lognode.mod_time)
        return false; // no folded function at this point

    *depth = lognode.call_depth + 1;
    *firstline = visnode.trace_file_firstline + visnode.trace_file_lines;
    *lastline = lognode.trace_file_firstline + lognode.trace_file_lines - 1;
    return true;
}

auto Browser::TraceView::node_fold_state(SeqOrderPayload &node) -> NodeFoldState
{
    SeqOrderPayload succ;
    if (br.get_next_node(node, &succ) && succ.call_depth > node.call_depth) {
        // This node's physical successor is at a higher call depth,
        // so it's a function call. To find out if it's folded, see if
        // its _visible_ successor is also its physical one.
        SeqOrderPayload vsucc;
        if (next_visible_node(node, &vsucc) &&
            vsucc.trace_file_firstline == succ.trace_file_firstline)
            return NodeFoldState::Unfolded;
        else
            return NodeFoldState::Folded;
    }
    // Otherwise, this isn't a function call at all, folded or otherwise.
    return NodeFoldState::Leaf;
}

static constexpr size_t decimal_size(RegPrefix pfx)
{
    return (pfx == RegPrefix::s ? 15 : pfx == RegPrefix::d ? 24 : 0);
}

size_t format_reg_length(const RegisterId &r)
{
    size_t len = 2 * reg_size(r); // a good starting point

    switch (r.prefix) {
    case RegPrefix::psr:
        len += 7; // " [NZCV]"
        break;
    case RegPrefix::s:
    case RegPrefix::d:
        len += 2 + decimal_size(r.prefix) + 1; // " [decimal-representation]"
        break;
    case RegPrefix::vpr:
        len += 24; // " [mask:????????????????]"
        break;
    default:
        break;
    }

    return len;
}

void Browser::format_reg(string &dispstr, string &disptype, const RegisterId &r,
                         off_t memroot, off_t diff_memroot,
                         unsigned diff_minline)
{
    unsigned iflags = get_iflags(memroot);
    Addr roffset = reg_offset(r, iflags);
    size_t rsize = reg_size(r);
    vector<unsigned char> val(rsize), def(rsize);
    getmem(memroot, 'r', roffset, rsize, &val[0], &def[0]);

    dispstr = disptype = "";
    dispstr += reg_name(r);
    dispstr += "=";
    type_extend(disptype, dispstr, 'f');

    size_t valstart = dispstr.size();

    bool highlight_diff = false;
    if (diff_memroot) {
        Addr diff_lo, diff_hi;
        if (find_next_mod(diff_memroot, 'r', roffset, diff_minline, +1, diff_lo,
                          diff_hi) &&
            diff_lo < roffset + rsize)
            highlight_diff = true;
    }

    int dh = highlight_diff ? 'A' - 'a' : 0;
    bool all_defined = true;
    unsigned long long intval = 0;

    for (int j = rsize; j-- > 0;) {
        char buf[3];
        if (!def[j]) {
            sprintf(buf, "??");
            all_defined = false;
        } else {
            sprintf(buf, "%02x", (unsigned)val[j]);
            intval = (intval << 8) | val[j];
        }
        dispstr += buf;
        type_extend(disptype, dispstr, (def[j] ? 'v' : 'u') + dh);
    }

    switch (r.prefix) {
    case RegPrefix::psr:
        dispstr += " [";
        type_extend(disptype, dispstr, 'f');
        {
            size_t offset = rsize - 1;
            dispstr += !def[offset] ? "?" : val[offset] & 0x80 ? "N" : "n";
            dispstr += !def[offset] ? "?" : val[offset] & 0x40 ? "Z" : "z";
            dispstr += !def[offset] ? "?" : val[offset] & 0x20 ? "C" : "c";
            dispstr += !def[offset] ? "?" : val[offset] & 0x10 ? "V" : "v";
            type_extend(disptype, dispstr, def[offset] ? 'v' : 'u');
        }
        dispstr += "]";
        type_extend(disptype, dispstr, 'f');
        break;

    case RegPrefix::s:
    case RegPrefix::d:
        dispstr += " [";
        type_extend(disptype, dispstr, 'f');

        if (all_defined) {
            string decimal = (r.prefix == RegPrefix::d ? double_btod(intval)
                                                       : float_btod(intval));
            dispstr += rpad(decimal, decimal_size(r.prefix));
            type_extend(disptype, dispstr, 'v' + dh);
        } else {
            dispstr += rpad("", decimal_size(r.prefix), '?');
            type_extend(disptype, dispstr, 'u' + dh);
        }

        dispstr += "]";
        type_extend(disptype, dispstr, 'f');
        break;

    case RegPrefix::vpr: {
        // We dump the current MVE predication mask in the form of T
        // and e (case difference for visual distinguishability).
        //
        // We don't dump the current VPT block position, partly
        // because it's too confusing (there are two separate 4-bit
        // fields running in parallel for different beats), and also
        // because it shouldn't be necessary anyway: the disassembly
        // in the trace file should already show the pattern of T and
        // E suffixes on the instructions.
        //
        // However, we do check whether there's a VPT block active at
        // all before we dump the mask.
        bool in_vpt_block = (def[2] && val[2]);

        dispstr += " [mask:";
        type_extend(disptype, dispstr, 'f');
        for (unsigned i = 16; i-- > 0;) {
            unsigned byte = i / 8, bit = i % 8;
            if (!in_vpt_block) {
                dispstr += "-";
                disptype += "v";
            } else if (!def[byte]) {
                dispstr += "?";
                disptype += "u";
            } else {
                dispstr += (1 & (val[byte] >> bit)) ? "T" : "e";
                disptype += "v";
            }
        }
        dispstr += "]";
        type_extend(disptype, dispstr, 'f');
        break;
    }

    default:
        break;
    }

    size_t expected_len = valstart + format_reg_length(r);
    assert(dispstr.size() == expected_len);
    assert(disptype.size() == expected_len);
}

void Browser::format_memory_split(string &dispaddr, string &typeaddr,
                                  string &disphex, string &typehex,
                                  string &dispchars, string &typechars,
                                  Addr addr, int bytes_per_line, int addr_chars,
                                  off_t memroot, off_t diff_memroot,
                                  unsigned diff_minline)
{
    dispaddr.clear();
    typeaddr.clear();
    disphex.clear();
    typehex.clear();
    dispchars.clear();
    typechars.clear();

    {
        char buf[32];
        sprintf(buf, "%0*llx", addr_chars, (unsigned long long)addr);
        dispaddr += buf;
        type_extend(typeaddr, dispaddr, 'f'); // fixed content
    }

    Addr diff_lo, diff_hi;
    bool got_diff =
        diff_memroot && find_next_mod(diff_memroot, 'm', addr, diff_minline, +1,
                                      diff_lo, diff_hi);

    int prev_dh = 0;
    for (int b = 0; b < bytes_per_line; b++) {
        if (diff_memroot && got_diff && diff_hi < addr) {
            got_diff = find_next_mod(diff_memroot, 'm', addr, diff_minline, +1,
                                     diff_lo, diff_hi);
        }

        unsigned char val, def;
        getmem(memroot, 'm', addr, 1, &val, &def);
        int dh =
            (got_diff && addr >= diff_lo && addr <= diff_hi ? 'A' - 'a' : 0);

        if (b > 0) {
            disphex += " ";
            if (dh && prev_dh)
                typehex += def ? "V" : "U";
            else
                typehex += def ? "v" : "u";
        }

        if (def) {
            char buf[4];
            sprintf(buf, "%02x", (unsigned)val);
            disphex += buf;
            type_extend(typehex, disphex, 'v' + dh);
            if (val >= 0x20 && val < 0x7F) {
                dispchars += (char)val;
                type_extend(typechars, dispchars, 'v' + dh);
            } else {
                dispchars += '.';
                type_extend(typechars, dispchars, 'c' + dh);
            }
        } else {
            disphex += "??";
            type_extend(typehex, disphex, 'u' + dh);
            dispchars += "?";
            type_extend(typechars, dispchars, 'u' + dh);
        }
        prev_dh = dh;
        addr++;
    }
}

void Browser::format_memory(string &line, string &type, Addr addr,
                            int bytes_per_line, int addr_chars, size_t &hexpos,
                            off_t memroot, off_t diff_memroot,
                            unsigned diff_minline)
{
    string dispaddr, typeaddr, disphex, typehex, dispchars, typechars;

    format_memory_split(dispaddr, typeaddr, disphex, typehex, dispchars,
                        typechars, addr, bytes_per_line, addr_chars, memroot,
                        diff_memroot, diff_minline);

    static const size_t separator_len = 2;
    static const string dispsep(separator_len, ' ');
    static const string typesep(separator_len, ' ');

    line = dispaddr + dispsep + disphex + dispsep + dispchars;
    type = typeaddr + typesep + typehex + typesep + typechars;
    hexpos = dispaddr.size() + dispsep.size();
}
