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

#ifndef TARMAC_BROWSER_BROWSE_HH
#define TARMAC_BROWSER_BROWSE_HH

#include "libtarmac/expr.hh"
#include "libtarmac/index.hh"
#include "libtarmac/memtree.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/registers.hh"

#include <memory>
#include <utility>

struct FoldStatePayload {
    unsigned first_physical_line, last_physical_line;
    // first_quasivis_line gives the indiex of what _would_ be the
    // first visible line in this region, if the entire buffer was at
    // this particular min/max depth. In other words, this is a value
    // you can pass to lrt_translate_range to compute offsets within
    // this region.
    unsigned first_quasivis_line;
    unsigned mindepth, maxdepth;
    unsigned n_physical_lines, n_visible_lines;
    int cmp(const FoldStatePayload &rhs) const;
};
struct FoldStateAnnotation {
    unsigned n_physical_lines{0}, n_visible_lines{0};
    FoldStateAnnotation() = default;
    FoldStateAnnotation(const FoldStateAnnotation &) = default;
    FoldStateAnnotation(const FoldStatePayload &payload)
        : n_physical_lines(payload.n_physical_lines),
          n_visible_lines(payload.n_visible_lines)
    {
    }
    FoldStateAnnotation(const FoldStateAnnotation &lhs,
                        const FoldStateAnnotation &rhs)
        : n_physical_lines(lhs.n_physical_lines + rhs.n_physical_lines),
          n_visible_lines(lhs.n_visible_lines + rhs.n_visible_lines)
    {
    }
};

class Browser : public IndexNavigator {
    using IndexNavigator::IndexNavigator;

  public:
    class TraceView {
        friend class Browser;
        friend struct TraceExecutionContext;

      public:
        Browser &br;

      private:
        IndexReader &index;

      public:
        // curr_visible_node is the node that we're actually
        // displaying with our 'current position' underline below it.
        //
        // curr_logical_node is the node that the memory and register
        // windows will be displaying the state after.
        //
        // In the absence of folded-up function calls, they'll be the
        // same node. However, if curr_visible_node is the BL
        // preceding a folded call section, then curr_logical_node
        // will point at the corresponding return, so that the
        // underline is conceptually just after the _whole function
        // call_ and shows the state of all registers just after the
        // function has returned.
        SeqOrderPayload curr_visible_node, curr_logical_node;

      private:
        AVLMem<FoldStatePayload, FoldStateAnnotation> fold_states;

        // Compute what update_logical_node would set the logical node to.
        void visible_to_logical_node(SeqOrderPayload &visnode,
                                     SeqOrderPayload *lognode);

        bool lookup_register(const RegisterId &r, uint64_t &out);

      public:
        TraceView(Browser &br);

        unsigned visible_to_physical_line(unsigned visline);
        unsigned physical_to_visible_line(unsigned physline);
        unsigned total_visible_lines();
        bool get_node_by_visline(unsigned visline, SeqOrderPayload *node,
                                 unsigned *offset_within_node = NULL);

        bool goto_time(Time t);
        bool goto_physline(unsigned line);
        bool goto_visline(unsigned line);
        bool goto_buffer_limit(bool end);
        bool goto_pc(unsigned long long pc, int dir);

        bool position_hidden();
        bool get_current_pc(unsigned long long &pc);

        // Set curr_visible_node correctly based on curr_logical_node.
        void update_visible_node();
        // Set curr_logical_node correctly based on curr_visible_node.
        void update_logical_node();

        // Return the next/previous visible node from a given point.
        bool next_visible_node(SeqOrderPayload &node, SeqOrderPayload *ret);
        bool prev_visible_node(SeqOrderPayload &node, SeqOrderPayload *ret);

        // Return the next/previous visible node from curr_visible_node.
        bool next_visible_node(SeqOrderPayload *ret);
        bool prev_visible_node(SeqOrderPayload *ret);

        // Find the fold state of a particular node: is it the start
        // of a function call, and if so, is it folded or unfolded?
        enum class NodeFoldState { Folded, Unfolded, Leaf };
        NodeFoldState node_fold_state(SeqOrderPayload &node);

        // Both of these return the depth corresponding to just the
        // function in question being unfolded, and none of its
        // subfunctions.
        bool physline_range_for_containing_function(SeqOrderPayload &node,
                                                    unsigned *firstline,
                                                    unsigned *lastline,
                                                    unsigned *depth);
        bool physline_range_for_folded_function_after(SeqOrderPayload &node,
                                                      unsigned *firstline,
                                                      unsigned *lastline,
                                                      unsigned *depth);

        void set_fold_state(unsigned firstline, unsigned lastline,
                            unsigned mindepth, unsigned maxdepth);

        // This function can evaluate an expression which refers to
        // symbols in the image, and also the values of registers at
        // the TraceView's current time.
        Addr evaluate_expression_addr(const std::string &line);
        Addr evaluate_expression_addr(ExprPtr expr);
    };

    Browser(const Browser &) = delete;
    Browser(Browser &&) = delete;

    bool get_node_by_physline(unsigned physline, SeqOrderPayload *node,
                              unsigned *offset_within_node = NULL);

    // Fills in dispstr with a string of the form 'regname=value',
    // where 'value' is formatted appropriately for the register type.
    // Fills in disptype with a parallel string whose characters are:
    //
    //  'r': part of the register name
    //  'f': other fixed text, such as the '=' separator or other punctuation
    //  'v': a character representing a defined part of the value
    //  'u': a character representing an undefined part of the value
    //  'V','U': same as 'v','u' but indicate that this character has
    //           changed its value between memroot and diff_memroot.
    void format_reg(std::string &dispstr, std::string &disptype,
                    const RegisterId &r, OFF_T memroot, OFF_T diff_memroot = 0,
                    unsigned diff_minline = 0);

    // Similar, but fills in the same output variables with a hex dump
    // of memory. One extra value pair can occur in disptype:
    //
    //  'c','C': used in place of v,V on the ASCII side of the hex dump
    //           to indicate that this is not a printable character.
    //           (Permits distinguishing a literal '.' from the '.' used
    //           to substitute for control chars.)
    //
    // The format_memory() function produces a whole line of hex dump,
    // containing all three of the address, hex and printable-
    // characters columns. 'hexpos' is filled in with the string
    // offset of the start of the first hex byte (for cursor
    // positioning etc).
    //
    // The format_memory_split() function produces the same text, but
    // in three separate strings, for frontends that display them
    // separately. (In which case, 'hexpos' is not needed, because
    // it's the start of the hex column.)
    void format_memory(std::string &dispstr, std::string &disptype, Addr addr,
                       bool addr_known, int bytes_per_line, int addr_chars,
                       size_t &hexpos, OFF_T memroot, OFF_T diff_memroot = 0,
                       unsigned diff_minline = 0);
    void format_memory_split(std::string &dispaddr, std::string &typeaddr,
                             std::string &disphex, std::string &typehex,
                             std::string &dispchars, std::string &typechars,
                             Addr addr, bool addr_known, int bytes_per_line,
                             int addr_chars, OFF_T memroot,
                             OFF_T diff_memroot = 0, unsigned diff_minline = 0);

    bool lookup_register(const std::string &name, RegisterId &r);

    // This version of evaluate_expression_addr() still lets you refer
    // to image symbols, but unlike TraceView's one, it doesn't let
    // you refer to registers.
    Addr evaluate_expression_addr(const std::string &line);
    Addr evaluate_expression_addr(ExprPtr expr);

    // And here's a parse_expression() that will refer to image symbols.
    ExprPtr parse_expression(const std::string &line,
                             std::ostringstream &error);
};

size_t format_reg_length(const RegisterId &r);

Addr evaluate_expression_general(const std::string &line,
                                 const ExecutionContext &ec);
Addr evaluate_expression_plain(const std::string &line);

class DecodedTraceLine : public ParseReceiver {
  public:
    std::unique_ptr<InstructionEvent> iev;
    std::unique_ptr<RegisterEvent> rev;
    std::unique_ptr<MemoryEvent> mev;
    DecodedTraceLine(const ParseParams &pparams, const std::string &line);

  private:
    virtual void got_event(MemoryEvent &ev) override;
    virtual void got_event(RegisterEvent &ev) override;
    virtual void got_event(InstructionEvent &ev) override;
};

class HighlightedLine : public ParseReceiver {
  public:
    std::string text;
    size_t display_len;
    size_t disassembly_start;
    std::vector<HighlightClass> highlights;
    std::unique_ptr<InstructionEvent> iev;
    bool non_executed_instruction;

    HighlightedLine(const std::string &text, const ParseParams &pparams,
                    size_t display_len);
    explicit HighlightedLine(const std::string &text,
                             const ParseParams &pparams);
    void replace_instruction(Browser &br);

    HighlightClass highlight_at(size_t i, bool enable_highlighting = true) const
    {
        if (!enable_highlighting || i >= highlights.size())
            return HL_NONE;
        HighlightClass hc = highlights[i];
        if (hc == HL_DISASSEMBLY && non_executed_instruction)
            hc = HL_CCFAIL;
        return hc;
    }

  private:
    virtual void highlight(size_t start, size_t end,
                           HighlightClass hc) override;
    virtual void got_event(InstructionEvent &ev) override;
};

void run_browser(Browser &br);

#endif // TARMAC_BROWSER_BROWSE_HH
