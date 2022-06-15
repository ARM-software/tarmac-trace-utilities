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

#ifndef LIBTARMAC_INDEX_HH
#define LIBTARMAC_INDEX_HH

#include "libtarmac/disktree.hh"
#include "libtarmac/image.hh"
#include "libtarmac/index_ds.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/registers.hh"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

void run_indexer(const TracePair &trace, bool big_endian,
                 bool show_progress_meter);
bool magic_number_ok(const std::string &index_filename);

class IndexReader {
    const std::string index_filename;
    const std::string tarmac_filename;
    MMapFile mmf;
    std::ifstream tarmac;
    bool bigend, aarch64_used;

    std::string read_tarmac(OFF_T pos, OFF_T len);

  public:
    AVLDisk<MemoryPayload, MemoryAnnotation> memtree;
    AVLDisk<MemorySubPayload> memsubtree;
    AVLDisk<SeqOrderPayload, SeqOrderAnnotation> seqtree;
    AVLDisk<ByPCPayload> bypctree;
    OFF_T seqroot, bypcroot;
    unsigned lineno_offset;

    IndexReader(const TracePair &trace);

    const void *index_offset(OFF_T pos) const { return mmf.getptr<char>(pos); }

    OFF_T index_subtree_root(OFF_T pos) const
    {
        return *mmf.getptr<diskint<OFF_T>>(pos);
    }

    std::vector<std::string> get_trace_lines(const SeqOrderPayload &node);
    std::string get_trace_line(const SeqOrderPayload &node, unsigned lineno);

    const std::string &get_index_filename() const { return index_filename; }
    const std::string &get_tarmac_filename() const { return tarmac_filename; }

    bool isBigEndian() const { return bigend; }
    bool isAArch64() const { return aarch64_used; }
};

class IndexNavigator {
    std::shared_ptr<Image> image;

  public:
    IndexReader index;

    IndexNavigator(const TracePair &trace,
                   std::shared_ptr<Image> image = nullptr)
        : image(image), index(trace)
    {
    }

    IndexNavigator(const TracePair &trace, const std::string &image_filename)
        : IndexNavigator(trace, image_filename.empty()
                                    ? nullptr
                                    : std::make_shared<Image>(image_filename))
    {
    }

    IndexNavigator(const IndexNavigator &) = delete;
    IndexNavigator(IndexNavigator &&) = delete;

    const std::string &get_tarmac_filename() const
    {
        return index.get_tarmac_filename();
    }
    const std::string &get_index_filename() const
    {
        return index.get_index_filename();
    }

    bool has_image() const { return bool(image); }
    std::shared_ptr<Image> get_image() const { return image; }

    bool lookup_symbol(const std::string &name, uint64_t &addr) const;
    bool lookup_symbol(const std::string &name, uint64_t &addr,
                       size_t &size) const;

    std::string get_symbolic_address(Addr addr, bool fallback = false);

    // Read the system's raw memory representation at a given time.
    // Return value is the line number of the latest trace event that
    // wrote any part of that data.
    unsigned getmem(OFF_T memroot, char type, Addr addr, size_t size,
                    void *outdata, unsigned char *outdef) const;

    // Read the raw memory representation, and last-update indication,
    // of the first defined subregion of the specified region. Returns
    // false if no such subregion exists.
    bool getmem_next(OFF_T memroot, char type, Addr addr, size_t size,
                     const void **outdata, Addr *outaddr, size_t *outsize,
                     unsigned *outline) const;

    // Read the iflags at a given time.
    unsigned get_iflags(OFF_T memroot) const;

    // Read a particular register. get_reg_bytes returns the raw bytes
    // of the register, whereas get_reg_value returns it as a C
    // integer. Both functions fail if the register's value is not
    // fully defined; get_reg_value also fails if the register is too
    // large to fit in an integer type.
    bool get_reg_bytes(OFF_T memroot, const RegisterId &reg,
                       std::vector<unsigned char> &val) const;
    std::pair<bool, uint64_t> get_reg_value(OFF_T memroot,
                                            const RegisterId &reg) const;

    bool node_at_time(Time t, SeqOrderPayload *node) const;
    bool node_at_line(unsigned line, SeqOrderPayload *node) const;
    bool get_previous_node(SeqOrderPayload &in, SeqOrderPayload *out) const;
    bool get_next_node(SeqOrderPayload &in, SeqOrderPayload *out) const;
    bool find_buffer_limit(bool end, SeqOrderPayload *node) const;
    bool find_next_mod(OFF_T memroot, char type, Addr addr, unsigned minline,
                       int sign, Addr &lo, Addr &hi);

    // Do a raw lookup in the layered range tree that indexes
    // trace lines by function call depth.
    //
    // The semantics are: find the (line)th line of the trace file
    // (counting from zero) whose call depth is in the range
    // [mindepth_i,maxdepth_i), and return the number of lines
    // preceding that one whose call depth are in the range
    // [mindepth_o,maxdepth_o).
    unsigned lrt_translate(unsigned line, unsigned mindepth_i,
                           unsigned maxdepth_i, unsigned mindepth_o,
                           unsigned maxdepth_o);

    // The above call assumes the search will succeed. If there's a
    // chance of it being out of range, use this call instead, which
    // returns <true, answer> on success, or <false, 0> if the search
    // fails.
    std::pair<bool, unsigned> lrt_translate_may_fail(unsigned line,
                                                     unsigned mindepth_i,
                                                     unsigned maxdepth_i,
                                                     unsigned mindepth_o,
                                                     unsigned maxdepth_o);

    // Convenience wrapper to take the difference of two lrt_translate
    // calls.
    //
    // Let S be the (linestart)th line with call depth in the input
    // range, and E be the (lineend)th one. Then the return value is
    // the number of lines in the range [S,E) whose call depth
    // is in the output range.
    unsigned lrt_translate_range(unsigned linestart, unsigned lineend,
                                 unsigned mindepth_i, unsigned maxdepth_i,
                                 unsigned mindepth_o, unsigned maxdepth_o);
};

#endif // LIBTARMAC_INDEX_HH
