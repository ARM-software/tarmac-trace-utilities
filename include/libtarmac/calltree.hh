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

#ifndef TARMAC_CALLTREE_HH
#define TARMAC_CALLTREE_HH

#include "libtarmac/index.hh"

#include <ostream>
#include <sstream>
#include <string>
#include <vector>

struct TarmacSite {
    Addr addr;            // PC address
    Time time;            // Time
    unsigned tarmac_line; // Line number in the trace
    off_t tarmac_pos;     // Offset (in bytes) in the trace

    constexpr TarmacSite(Addr addr, Time time, unsigned tarmac_line,
                         off_t tarmac_pos)
        : addr(addr), time(time), tarmac_line(tarmac_line),
          tarmac_pos(tarmac_pos)
    {
    }
    constexpr TarmacSite(Addr addr, unsigned line)
        : addr(addr), time(0), tarmac_line(line), tarmac_pos(0)
    {
    }
    constexpr TarmacSite() : addr(0), time(0), tarmac_line(0), tarmac_pos(0) {}
    TarmacSite(const TarmacSite &) = default;
    TarmacSite &operator=(const TarmacSite &ts) = default;

    TarmacSite(const SeqOrderPayload &sop)
    {
        addr = sop.pc;
        time = sop.mod_time;
        tarmac_line = sop.trace_file_firstline;
        tarmac_pos = sop.trace_file_pos;
    }

    TarmacSite &operator=(const SeqOrderPayload &sop)
    {
        addr = sop.pc;
        time = sop.mod_time;
        tarmac_line = sop.trace_file_firstline;
        tarmac_pos = sop.trace_file_pos;
        return *this;
    }
};

class CallTree;
class CallTreeVisitor {
  protected:
    const CallTree &CT;

  public:
    CallTreeVisitor(const CallTree &CT) : CT(CT) {}
    void onFunctionEntry(const TarmacSite &function_entry,
                         const TarmacSite &function_exit)
    {
    }
    void onFunctionExit(const TarmacSite &function_entry,
                        const TarmacSite &function_exit)
    {
    }
    void onCallSite(const TarmacSite &function_entry,
                    const TarmacSite &function_exit,
                    const TarmacSite &call_site, const TarmacSite &resume_site,
                    const CallTree &TC)
    {
    }
    void onResumeSite(const TarmacSite &function_entry,
                      const TarmacSite &function_exit,
                      const TarmacSite &resume_site)
    {
    }
};

class CallTree {
    IndexNavigator &IN;

    // The first instruction in this function instance.
    TarmacSite function_entry;
    // The last instruction in this function call instance, usually a return.
    TarmacSite function_exit;

    // The sites to sub calls in this function call instance.
    std::vector<TarmacSite> call_sites;
    // The sites where the subcalls returned to in this function call instance.
    std::vector<TarmacSite> resume_sites;
    // The calltrees of all functions called by this function call instance.
    std::vector<CallTree> call_trees;

  public:
    CallTree(IndexNavigator &IN);
    CallTree(IndexNavigator &IN, const TarmacSite &site)
        : IN(IN), function_entry(site), function_exit(), call_sites(),
          resume_sites(), call_trees()
    {
    }

    void setFunctionExit(const TarmacSite &site) { function_exit = site; }
    void setFunctionEntry(const TarmacSite &site) { function_entry = site; }
    const TarmacSite &getFunctionExit() const { return function_exit; }
    const TarmacSite &getFunctionEntry() const { return function_entry; }

    unsigned getNumCalls() const { return call_sites.size(); }

    CallTree *addCallSite(const TarmacSite &call_site,
                          const TarmacSite &call_target)
    {
        call_sites.push_back(call_site);
        call_trees.emplace_back(IN, call_target);
        return &call_trees.back();
    }
    void addResumeSite(const TarmacSite &resume_site)
    {
        resume_sites.push_back(resume_site);
    }

    std::string getFunctionName(Addr addr) const;
    std::string getFunctionName(const TarmacSite &site) const;
    void csdump(std::ostream &os, const TarmacSite &site) const;
    void dump(unsigned level = 0) const;

    /*
     * Generate a file in the 'folded' format that the script in
     * https://github.com/brendangregg/FlameGraph accepts.
     *
     * This file format represents a multiset of call stacks, and is
     * designed to be generated from many different kinds of input, as
     * long as you have _some_ way to query the program's full call
     * stack every so often. Each line consists of a call stack in the
     * form of function identifiers, separated by semicolons, from
     * outermost to innermost; then a space character, followed by the
     * number of samples in the input data which had that exact call
     * stack.
     *
     * Here, we have a full instruction-by-instruction trace of the
     * whole execution, so we can generate the most precise data
     * possible, in which there is one 'sample' per unit of time in
     * the Tarmac trace.
     */
    void generate_flame_graph(std::ostream &os) const;

    // Visit the calltree in chronological order.
    template <typename Visitor = CallTreeVisitor> void visit(Visitor &V) const
    {
        V.onFunctionEntry(function_entry, function_exit);

        unsigned N = getNumCalls();
        for (unsigned i = 0; i < N; i++) {
            V.onCallSite(function_entry, function_exit, call_sites[i],
                         resume_sites[i], call_trees[i]);
            call_trees[i].visit(V);
            V.onResumeSite(function_entry, function_exit, resume_sites[i]);
        }

        V.onFunctionExit(function_entry, function_exit);
    }

    // Visit the calltree in reverse-chronological order.
    template <typename Visitor = CallTreeVisitor> void rvisit(Visitor &V) const
    {
        V.onFunctionExit(function_entry, function_exit);

        unsigned N = getNumCalls();
        for (unsigned i = 0; i < N; i++) {
            unsigned j = N - 1 - i;
            V.onResumeSite(function_entry, function_exit, resume_sites[j]);
            call_trees[j].rvisit(V);
            V.onCallSite(function_entry, function_exit, call_sites[j],
                         resume_sites[j], call_trees[j]);
        }

        V.onFunctionEntry(function_entry, function_exit);
    }

  private:
    // Recursive helper function used by generate_flame_graph().
    void generate_flame_graph_recurse(std::ostringstream &stackstream,
                                      std::map<std::string, Time> &output,
                                      Time *parent_time) const;
};

#endif // TARMAC_CALLTREE_HH
