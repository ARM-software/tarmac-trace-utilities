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

#include "libtarmac/calltree.hh"
#include "libtarmac/image.hh"
#include "libtarmac/misc.hh"

#include <climits>
#include <iostream>
#include <sstream>
#include <vector>

using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::map;
using std::min;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

string CallTree::getFunctionName(Addr addr) const
{
    if (IN.has_image())
        if (const Symbol *Symb = IN.get_image()->find_symbol(addr))
            return Symb->name;
    return string();
}

string CallTree::getFunctionName(const TarmacSite &site) const
{
    return getFunctionName(site.addr);
}

void CallTree::csdump(ostream &os, const TarmacSite &site) const
{
    os << "t:" << site.time;
    os << " l:" << (site.tarmac_line + IN.index.lineno_offset);
    os << " pc:0x" << hex << site.addr << dec;
}

void CallTree::dump(unsigned level) const
{
    cout << string(level * 2, ' ');
    cout << "o ";
    csdump(cout, function_entry);
    cout << " - ";
    csdump(cout, function_exit);
    cout << " : " << getFunctionName(function_entry);
    cout << '\n';
    unsigned N = getNumCalls();
    for (unsigned i = 0; i < N; i++) {
        cout << string((level + 1) * 2, ' ');
        cout << "- ";
        csdump(cout, call_sites[i]);
        cout << " - ";
        csdump(cout, resume_sites[i]);
        cout << '\n';
        call_trees[i].dump(level + 2);
    }
}

void CallTree::generate_flame_graph_recurse(ostringstream &stackstream,
                                            map<string, Time> &output,
                                            Time *parent_time) const
{
    // Remember the current size of the stack string stored in our
    // ostringstream, so we can reset it at the end of this function.
    size_t parent_size = stackstream.tellp();

    // Semicolon separator between the previous text (if any) and our
    // new function name.
    if (parent_size > 0)
        stackstream << ';';

    // Add our function name to the end of the list, falling back to a
    // hex function address if the actual name is unavailable.
    string fn = getFunctionName(function_entry);
    if (!fn.empty())
        stackstream << fn;
    else
        stackstream << "0x" << hex << function_entry.addr;

    // Count up the total time we spend in this function call. We
    // actually want the time spent physically *in* this function, not
    // counting subroutines, so the recursive calls in the loop below
    // will subtract each subroutine's duration from total_time.
    Time total_time = function_exit.time - function_entry.time;

    // ... and therefore, we must adjust _our_ parent's total_time in
    // the same way (if we have a parent at all).
    if (parent_time)
        *parent_time -= total_time;

    // Iterate over all subroutines, appending their stacks to the
    // output map, and adjusting our total_time to exclude their
    // durations.
    for (unsigned i = 0, N = getNumCalls(); i < N; i++)
        call_trees[i].generate_flame_graph_recurse(stackstream, output,
                                                   &total_time);

    // Add our call stack to the map, with multiplicity equal to the
    // final value of total_time after subroutines were subtracted.
    //
    // There doesn't seem to be an API call to truncate the contents
    // of an ostringstream, so instead, I truncate it immediately
    // after pulling it out into a std::string.
    string stack_text = stackstream.str().substr(0, stackstream.tellp());
    output[stack_text] += total_time;

    // Restore the ostringstream's output position to where it was
    // when we entered this function.
    stackstream.seekp(parent_size);
}

void CallTree::generate_flame_graph(ostream &os) const
{
    ostringstream stackstream;
    map<string, unsigned> output;
    generate_flame_graph_recurse(stackstream, output, nullptr);
    for (auto &kv : output)
        os << kv.first << ' ' << kv.second << endl;
}

namespace {
class CallDepthTracker {
    vector<CallTree *> CT;
    CallTree &CTRoot;
    int depth;

  public:
    CallDepthTracker(CallTree &CTRoot) : CTRoot(CTRoot), depth(-1)
    {
        CT.push_back(&CTRoot);
    }

    void start(const SeqOrderPayload &initialNode)
    {
        // We are at the CallTree root. Perform some initialize as root
        // needs some different handling.
        CTRoot.setFunctionEntry(initialNode);
        depth = initialNode.call_depth;
    }

    void newDepth(const SeqOrderPayload &newNode,
                  const SeqOrderPayload &prevNode)
    {
        if (newNode.call_depth > depth) {
            // A function call !
            CallTree *SubCallTree = CT.back()->addCallSite(prevNode, newNode);
            CT.push_back(SubCallTree);
        } else if (newNode.call_depth < depth) {
            // A function return !
            CT.back()->setFunctionExit(prevNode);
            CT.pop_back();
            CT.back()->addResumeSite(newNode);
        }
        depth = newNode.call_depth;
    }

    void finish(const SeqOrderPayload &finalNode)
    {
        if (depth == 0)
            CT.back()->setFunctionExit(finalNode);
    }
};
} // namespace

CallTree::CallTree(IndexNavigator &IN)
    : IN(IN), function_entry(), function_exit(), call_sites(), resume_sites(),
      call_trees()
{
    CallDepthTracker tracker(*this);

    unsigned line = 0;
    SeqOrderPayload node;

    // Skip first lines which have an invalid PC.
    while (IN.node_at_line(line + 1, &node) && node.pc == KNOWN_INVALID_PC)
        line++;

    bool initializeTracker = true;
    while (IN.node_at_line(line + 1, &node)) {
        if (initializeTracker) {
            tracker.start(node);
            initializeTracker = false;
        } else {
            SeqOrderPayload prevNode;
            bool success = IN.get_previous_node(node, &prevNode);
            (void)success; // squash compiler warning if asserts compiled out
            assert(success);
            tracker.newDepth(node, prevNode);
        }

        unsigned depth = node.call_depth;

        bool found = false;
        unsigned x, nextline = UINT_MAX;
        pair<bool, unsigned> searchresult;

        // How many lines in the trace file are at a higher depth than
        // this one?
        x = IN.lrt_translate(line, 0, UINT_MAX, depth + 1, UINT_MAX);
        // Find the _next_ line at a higher depth.
        searchresult =
            IN.lrt_translate_may_fail(x, depth + 1, UINT_MAX, 0, UINT_MAX);
        if (searchresult.first) {
            found = true;
            nextline = min(nextline, searchresult.second);
        }

        if (depth > 0) {
            // Same with a lower depth (if there is a lower depth).
            x = IN.lrt_translate(line, 0, UINT_MAX, 0, depth);
            searchresult = IN.lrt_translate_may_fail(x, 0, depth, 0, UINT_MAX);
            if (searchresult.first) {
                found = true;
                nextline = min(nextline, searchresult.second);
            }
        }

        if (!found)
            break;

        line = nextline;
    }

    SeqOrderPayload finalNode;
    bool success = IN.find_buffer_limit(true, &finalNode);
    (void)success; // squash compiler warning if asserts compiled out
    assert(success);
    tracker.finish(finalNode);
}
