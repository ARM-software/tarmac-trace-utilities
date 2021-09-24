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

#include "libtarmac/argparse.hh"
#include "libtarmac/index.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <climits>
#include <functional>
#include <iostream>
#include <stack>
#include <string>
#include <vector>
using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::min;
using std::showbase;
using std::stack;
using std::string;
using std::vector;

static bool omit_index_offsets;

static void dump_memory_at_line(const IndexNavigator &IN, unsigned trace_line,
                                const std::string &prefix);

template <typename Payload, typename Annotation> class TreeDumper {
    struct StackEntry {
        bool first_of_two;
        bool right_child;
        string prefix;
    };

    string prefix;
    stack<StackEntry> stk;

    virtual void dump_payload(const string &prefix, const Payload &) = 0;
    virtual void dump_annotation(const string &prefix, const Payload &,
                                 const Annotation &)
    {
    }

    virtual void node_header(off_t offset)
    {
        cout << prefix << "Node";
        if (!omit_index_offsets)
            cout << prefix << " at file offset " << offset;
        cout << ":" << endl;
    }

    void visit(const Payload &payload, off_t offset)
    {
        node_header(offset);
        dump_payload(prefix + "    ", payload);
    }

    void walk(const Payload &payload, const Annotation &annotation,
              off_t leftoff, const Annotation *, off_t rightoff,
              const Annotation *, off_t offset)
    {
        string firstlineprefix, finalprefix, node_type;

        if (!stk.empty()) {
            StackEntry &pop = stk.top();
            prefix = pop.prefix;

            if (pop.first_of_two) {
                firstlineprefix = prefix + "├── ";
                prefix += "│   ";
                finalprefix = prefix;
            } else {
                firstlineprefix = prefix + "└── ";
                finalprefix = prefix;
                prefix += "    ";
            }
            node_type =
                pop.right_child ? "Right child node" : "Left child node";
            stk.pop();
        } else {
            node_type = "Root node";
            firstlineprefix = finalprefix = prefix;
        }

        cout << firstlineprefix << hex << node_type << " at file offset "
             << offset << ":" << endl;

        if (rightoff) {
            stk.push({false, true, prefix});
            if (leftoff)
                stk.push({true, false, prefix});
        } else if (leftoff) {
            stk.push({false, false, prefix});
        }

        if (rightoff || leftoff)
            prefix += "│   ";
        else
            prefix += "    ";

        cout << prefix << "Child offsets = { ";
        if (leftoff)
            cout << leftoff;
        else
            cout << "null";
        cout << ", ";
        if (rightoff)
            cout << rightoff;
        else
            cout << "null";
        cout << " }" << endl << dec;

        dump_payload(prefix, payload);
        dump_annotation(prefix, payload, annotation);

        if (!stk.empty()) {
            // Leave a blank line before the next node, if we're expecting one.
            size_t useful_prefix_len =
                min((size_t)1, prefix.find_last_not_of(" ")) - 1;
            cout << prefix.substr(0, useful_prefix_len - 1) << endl;
        }

        prefix = finalprefix;
    }

  protected:
    const IndexNavigator &IN;

  public:
    class Visitor {
        TreeDumper *parent;

      public:
        Visitor(TreeDumper *parent) : parent(parent) {}
        void operator()(const Payload &payload, off_t offset)
        {
            parent->visit(payload, offset);
        }
    } visitor;

    class Walker {
        TreeDumper *parent;

      public:
        Walker(TreeDumper *parent) : parent(parent) {}
        void operator()(const Payload &payload, const Annotation &annotation,
                        off_t lo, const Annotation *la, off_t ro,
                        const Annotation *ra, off_t offset)
        {
            parent->walk(payload, annotation, lo, la, ro, ra, offset);
        }
    } walker;

    TreeDumper(const IndexNavigator &IN) : IN(IN), visitor(this), walker(this)
    {
    }
};

class SeqTreeDumper : public TreeDumper<SeqOrderPayload, SeqOrderAnnotation> {
    using TreeDumper::TreeDumper;

    virtual void dump_payload(const string &prefix,
                              const SeqOrderPayload &node) override
    {
        cout << prefix << "Line range: start " << node.trace_file_firstline
             << ", extent " << node.trace_file_lines << endl;
        cout << prefix << "Byte range: start " << hex << node.trace_file_pos
             << ", extent " << node.trace_file_len << dec << endl;
        cout << prefix << "Modification time: " << node.mod_time << endl;
        cout << prefix << "PC: ";
        if (node.pc == KNOWN_INVALID_PC)
            cout << "invalid";
        else
            cout << hex << node.pc << dec;
        cout << endl;
        if (!omit_index_offsets) {
            cout << prefix << "Root of memory tree: " << hex << node.memory_root
                 << dec << endl;
        }
        cout << prefix << "Call depth: " << node.call_depth << endl;
        if (dump_memory) {
            dump_memory_at_line(IN, node.trace_file_firstline, prefix + "  ");
        }
    }

    virtual void dump_annotation(const string &prefix,
                                 const SeqOrderPayload &node,
                                 const SeqOrderAnnotation &annotation) override
    {
        auto *array = (const CallDepthArrayEntry *)IN.index.index_offset(
            annotation.call_depth_array);

        for (unsigned i = 0, e = annotation.call_depth_arraylen; i < e; i++) {
            const CallDepthArrayEntry &ent = array[i];
            cout << prefix << "LRT[" << i << "] = { ";
            if (ent.call_depth == SENTINEL_DEPTH)
                cout << "sentinel";
            else
                cout << "depth " << ent.call_depth;
            cout << ", " << ent.cumulative_lines << " lines, "
                 << ent.cumulative_insns << " insns, "
                 << "left-crosslink " << ent.leftlink << ", "
                 << "right-crosslink " << ent.rightlink << "}" << endl;
        }
    }

  public:
    bool dump_memory;
};

class MemTreeDumper : public TreeDumper<MemoryPayload, MemoryAnnotation> {
    using TreeDumper::TreeDumper;

    virtual void dump_payload(const string &prefix,
                              const MemoryPayload &node) override
    {
        cout << prefix << "Range: ";
        // FIXME: to make diagnostic dumps less opaque, it would be
        // nice here to translate reg addresses back into some
        // meaningful identification involving a register number.
        if (node.type == 'r')
            cout << "register-space";
        else
            cout << "memory";
        cout << " [" << hex << node.lo << "-" << node.hi << dec << "]" << endl;
        cout << prefix << "Contents: ";
        if (node.raw) {
            cout << (node.hi + 1 - node.lo) << " bytes";
            if (!omit_index_offsets)
                cout << " at file offset " << hex << node.contents << dec;
        } else {
            cout << "memory subtree";
            if (!omit_index_offsets)
                cout << " with root pointer at " << hex << node.contents
                     << ", actual root is "
                     << IN.index.index_subtree_root(node.contents) << dec;
        }
        cout << endl;
        cout << prefix << "Last modification: ";
        if (node.trace_file_firstline == 0)
            cout << "never";
        else
            cout << "line " << node.trace_file_firstline;
        cout << endl;
    }

    virtual void dump_annotation(const string &prefix,
                                 const MemoryPayload &node,
                                 const MemoryAnnotation &annotation) override
    {
        cout << prefix << "Latest modification time in whole subtree: "
             << annotation.latest << endl;
    }
};

class MemSubtreeDumper
    : public TreeDumper<MemorySubPayload, EmptyAnnotation<MemorySubPayload>> {
    using TreeDumper::TreeDumper;

    virtual void dump_payload(const string &prefix,
                              const MemorySubPayload &node) override
    {
        // Here we _can't_ translate register-space addresses back
        // into registers, even if we wanted to, because we haven't
        // been told whether this subtree even refers to register or
        // memory space.
        cout << prefix << "Range: [" << hex << node.lo << "-" << node.hi << dec
             << "]" << endl;
        cout << prefix << "Contents: " << (node.hi + 1 - node.lo) << " bytes";
        if (!omit_index_offsets)
            cout << " at file offset " << hex << node.contents << dec;
        cout << endl;
    }
};

class ByPCTreeDumper
    : public TreeDumper<ByPCPayload, EmptyAnnotation<ByPCPayload>> {
    using TreeDumper::TreeDumper;

    virtual void dump_payload(const string &prefix,
                              const ByPCPayload &node) override
    {
        cout << prefix << "PC: " << hex << node.pc << dec << endl;
        cout << prefix << "Line: " << node.trace_file_firstline << endl;
    }
};

static const struct {
    const char *pname;
    RegPrefix prefix;
    unsigned nregs;
} reg_families[] = {
#define WRITE_ENTRY(prefix, ignore1, ignore2, nregs)                           \
    {#prefix, RegPrefix::prefix, nregs},
    REGPREFIXLIST(WRITE_ENTRY, WRITE_ENTRY)
#undef WRITE_ENTRY
};

static void dump_registers(bool got_iflags, unsigned iflags)
{
    for (const auto &fam : reg_families) {
        for (unsigned i = 0; i < fam.nregs; i++) {
            RegisterId r{fam.prefix, i};
            cout << reg_name(r);
            if (!got_iflags && reg_needs_iflags(r)) {
                cout << " - dependent on iflags\n";
            } else {
                cout << " offset=" << hex << reg_offset(r, iflags)
                     << " size=" << hex << reg_size(r) << "\n";
            }
        }
    }
}

static unsigned long long parseint(const string &s)
{
    try {
        size_t pos;
        unsigned long long toret = stoull(s, &pos, 0);
        if (pos < s.size())
            throw ArgparseError("'" + s + "': unable to parse numeric value");
        return toret;
    } catch (std::invalid_argument) {
        throw ArgparseError("'" + s + "': unable to parse numeric value");
    } catch (std::out_of_range) {
        throw ArgparseError("'" + s + "': numeric value out of range");
    }
}

static void hexdump(const void *vdata, size_t size, Addr startaddr,
                    const std::string &prefix)
{
    const unsigned char *data = (const unsigned char *)vdata;
    char linebuf[100], fmtbuf[32];
    constexpr Addr linelen = 16, mask = ~(linelen - 1);

    while (size > 0) {
        Addr lineaddr = startaddr & mask;
        size_t linesize = min(size, (size_t)(lineaddr + linelen - startaddr));
        memset(linebuf, ' ', 83);

        snprintf(fmtbuf, sizeof(fmtbuf), "%016llx",
                 (unsigned long long)lineaddr);
        memcpy(linebuf, fmtbuf, 16);
        size_t outlinelen = 16;

        for (size_t i = 0; i < linesize; i++) {
            size_t lineoff = i + (startaddr - lineaddr);

            size_t hexoff = 16 + 1 + 3 * lineoff;
            snprintf(fmtbuf, sizeof(fmtbuf), "%02x", (unsigned)data[i]);
            memcpy(linebuf + hexoff, fmtbuf, 2);

            size_t chroff = 16 + 1 + 3 * 16 + 1 + lineoff;
            linebuf[chroff] =
                (0x20 <= data[i] && data[i] < 0x7F ? data[i] : '.');
            outlinelen = chroff + 1;
        }

        linebuf[outlinelen] = '\0';
        cout << prefix << linebuf << endl;

        startaddr += linesize;
        size -= linesize;
    }
}

static void regdump(const std::vector<unsigned char> &val,
                    const std::vector<unsigned char> &def)
{
    for (size_t i = 0, e = min(val.size(), def.size()); i < e; i++) {
        if (i)
            cout << " ";
        if (def[i]) {
            char fmtbuf[3];
            snprintf(fmtbuf, sizeof(fmtbuf), "%02x", (unsigned)val[i]);
            cout << fmtbuf;
        } else {
            cout << "..";
        }
    }
}

static void dump_memory_at_line(const IndexNavigator &IN, unsigned trace_line,
                                const std::string &prefix)
{
    SeqOrderPayload node;
    if (!IN.node_at_line(trace_line, &node)) {
        cerr << "Unable to find a node at line " << trace_line << "\n";
        exit(1);
    }
    off_t memroot = node.memory_root;
    unsigned iflags = IN.get_iflags(memroot);

    const void *outdata;
    Addr outaddr;
    size_t outsize;
    unsigned outline;

    Addr readaddr = 0;
    size_t readsize = 0;
    while (IN.getmem_next(memroot, 'm', readaddr, readsize, &outdata, &outaddr,
                          &outsize, &outline)) {
        cout << prefix << "Memory last modified at line " << outline << ":"
             << endl;
        hexdump(outdata, outsize, outaddr, prefix);
        readsize -= outaddr + outsize - readaddr;
        readaddr = outaddr + outsize;
        if (!readaddr)
            break;
    }

    for (const auto &regfam : reg_families) {
        for (unsigned i = 0; i < regfam.nregs; i++) {
            RegisterId reg{regfam.prefix, i};
            size_t size = reg_size(reg);
            vector<unsigned char> val(size), def(size);

            unsigned mod_line = IN.getmem(memroot, 'r', reg_offset(reg, iflags),
                                          size, &val[0], &def[0]);

            bool print = false;
            for (auto c : def) {
                if (c) {
                    print = true;
                    break;
                }
            }

            if (print) {
                cout << prefix << reg_name(reg) << ", last modified at line "
                     << mod_line << ": ";
                regdump(val, def);
                cout << endl;
            }
        }
    }
}

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    enum class Mode {
        None,
        Header,
        SeqVisit,
        SeqVisitWithMem,
        SeqWalk,
        MemVisit,
        MemWalk,
        MemSubVisit,
        MemSubWalk,
        ByPCVisit,
        ByPCWalk,
        RegMap,
        FullMemByLine,
    } mode = Mode::None;
    off_t root;
    unsigned trace_line;
    unsigned iflags = 0;
    bool got_iflags = false;

    Argparse ap("tarmac-indextool", argc, argv);
    TarmacUtility tu(ap, false, false);

    ap.optnoval({"--header"}, "dump file header",
                [&]() { mode = Mode::Header; });
    ap.optnoval({"--seq"}, "dump logical content of the sequential order tree",
                [&]() { mode = Mode::SeqVisit; });
    ap.optnoval({"--seq-with-mem"},
                "dump logical content of the sequential "
                "order tree, and memory contents at each node",
                [&]() { mode = Mode::SeqVisitWithMem; });
    ap.optnoval({"--seqtree"},
                "dump physical structure of the sequential "
                "order tree",
                [&]() { mode = Mode::SeqWalk; });
    ap.optval({"--mem"}, "OFFSET",
              "dump logical content of memory tree with "
              "root at OFFSET",
              [&](const string &s) {
                  mode = Mode::MemVisit;
                  root = parseint(s);
              });
    ap.optval({"--memtree"}, "OFFSET",
              "dump physical structure of a memory "
              "tree with root at OFFSET",
              [&](const string &s) {
                  mode = Mode::MemWalk;
                  root = parseint(s);
              });
    ap.optval({"--memsub"}, "OFFSET",
              "dump logical content of a memory "
              "subtree with root at OFFSET",
              [&](const string &s) {
                  mode = Mode::MemSubVisit;
                  root = parseint(s);
              });
    ap.optval({"--memsubtree"}, "OFFSET",
              "dump physical structure of a memory"
              " subtree with root at OFFSET",
              [&](const string &s) {
                  mode = Mode::MemSubWalk;
                  root = parseint(s);
              });
    ap.optnoval({"--bypc"}, "dump logical content of the by-PC tree",
                [&]() { mode = Mode::ByPCVisit; });
    ap.optnoval({"--bypctree"}, "dump physical structure of the by-PC tree",
                [&]() { mode = Mode::ByPCWalk; });
    ap.optnoval({"--regmap"}, "write a memory map of the register space",
                [&]() { mode = Mode::RegMap; });
    ap.optval({"--iflags"}, "FLAGS",
              "(for --regmap) specify iflags context "
              "to retrieve registers",
              [&](const string &s) {
                  got_iflags = true;
                  iflags = parseint(s);
              });
    ap.optnoval({"--omit-index-offsets"},
                "do not dump offsets in index file "
                "(so that output is more stable when index format changes)",
                [&]() { omit_index_offsets = true; });
    ap.optval({"--full-mem-at-line"}, "OFFSET",
              "dump full content of memory "
              "tree corresponding to a particular line of the trace file",
              [&](const string &s) {
                  mode = Mode::FullMemByLine;
                  trace_line = parseint(s);
              });

    ap.parse([&]() {
        if (mode == Mode::None && !tu.only_index())
            throw ArgparseError("expected an option describing a query");
        if (mode != Mode::RegMap && tu.trace.tarmac_filename.empty())
            throw ArgparseError("expected a trace file name");
    });

    cout << showbase; // ensure all hex values have a leading 0x

    // Modes that don't need a trace file
    switch (mode) {
    case Mode::RegMap: {
        dump_registers(got_iflags, iflags);
        return 0;
    }

    default:
        // Exit this switch and go on to load the trace file
        break;
    }

    tu.setup();
    const IndexNavigator IN(tu.trace);

    switch (mode) {
    case Mode::None:
    case Mode::RegMap:
        assert(false && "This should have been ruled out above");

    case Mode::Header: {
        cout << "Endianness: " << (IN.index.isBigEndian() ? "big" : "little")
             << endl;
        cout << "Architecture: "
             << (IN.index.isAArch64() ? "AArch64" : "AArch32") << endl;
        cout << "Root of sequential order tree: " << IN.index.seqroot << endl;
        cout << "Root of by-PC tree: " << IN.index.bypcroot << endl;
        cout << "Line number adjustment for file header: "
             << IN.index.lineno_offset << endl;
        break;
    }

    case Mode::SeqVisit:
    case Mode::SeqVisitWithMem: {
        SeqTreeDumper d(IN);
        d.dump_memory = (mode == Mode::SeqVisitWithMem);
        IN.index.seqtree.visit(IN.index.seqroot, d.visitor);
        break;
    }

    case Mode::SeqWalk: {
        SeqTreeDumper d(IN);
        IN.index.seqtree.walk(IN.index.seqroot, WalkOrder::Preorder, d.walker);
        break;
    }

    case Mode::MemVisit: {
        MemTreeDumper d(IN);
        IN.index.memtree.visit(root, d.visitor);
        break;
    }

    case Mode::MemWalk: {
        MemTreeDumper d(IN);
        IN.index.memtree.walk(root, WalkOrder::Preorder, d.walker);
        break;
    }

    case Mode::MemSubVisit: {
        MemSubtreeDumper d(IN);
        IN.index.memsubtree.visit(root, d.visitor);
        break;
    }

    case Mode::MemSubWalk: {
        MemSubtreeDumper d(IN);
        IN.index.memsubtree.walk(root, WalkOrder::Preorder, d.walker);
        break;
    }

    case Mode::ByPCVisit: {
        ByPCTreeDumper d(IN);
        IN.index.bypctree.visit(IN.index.bypcroot, d.visitor);
        break;
    }

    case Mode::ByPCWalk: {
        ByPCTreeDumper d(IN);
        IN.index.bypctree.walk(IN.index.bypcroot, WalkOrder::Preorder,
                               d.walker);
        break;
    }

    case Mode::FullMemByLine: {
        dump_memory_at_line(IN, trace_line, "");
        break;
    }
    }

    return 0;
}
