/*
 * Copyright 2025 Arm Limited. All rights reserved.
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
#include "libtarmac/intl.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string.h>
#include <string>
#include <vector>

using std::begin;
using std::cin;
using std::cout;
using std::deque;
using std::end;
using std::endl;
using std::ifstream;
using std::istream;
using std::make_unique;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::string;
using std::unique_ptr;
using std::vector;

std::unique_ptr<Reporter> reporter = make_cli_reporter();

namespace {

class Reader : ParseReceiver {
    istream &is;
    ostream &os;
    string tarmac_filename;
    int lineno = 0;
    TarmacLineParser parser;

    size_t pc_loop_limit = 16;
    size_t text_event_loop_limit = 32;

    vector<uint8_t> register_space;

    deque<Addr> previous_pcs;
    deque<TextOnlyEvent> previous_text_events;

    Addr last_pc = KNOWN_INVALID_PC;
    bool reg_change_since_last_insn = false;
    bool still_reading = true;
    unsigned iflags;

    void register_changed() {
        // Clear the PC cache if a register changes
        previous_pcs.clear();
    }

    void got_event(InstructionEvent &ev) override
    {
        // Truncate on revisiting a PC since any event cleared the PC cache
        if (std::find(begin(previous_pcs), end(previous_pcs), ev.pc) !=
            end(previous_pcs))
            still_reading = false; // tight loop

        previous_pcs.push_back(ev.pc);
        if (previous_pcs.size() > pc_loop_limit)
            previous_pcs.pop_front();

        // Clear the text-event cache on any instruction
        previous_text_events.clear();

        if (ev.iset == A64)
            iflags |= IFLAG_AARCH64;
    }

    void got_event(RegisterEvent &ev) override
    {
        size_t start = reg_offset(ev.reg, iflags) + ev.offset;
        size_t size = ev.bytes.size();
        size_t end = start + size;
        if (end > register_space.size()) {
            register_space.resize(end, 0);
        } else {
            if (memcmp(register_space.data() + start, ev.bytes.data(), size))
                register_changed();
        }
        memcpy(register_space.data() + start, ev.bytes.data(), size);

        // Clear the text-event cache on any register update, change or not
        previous_text_events.clear();
    }

    void got_event(TextOnlyEvent &ev) override
    {
        previous_text_events.push_back(ev);
        if (previous_text_events.size() > text_event_loop_limit)
            previous_text_events.pop_front();

        // Look for a sequence of TextOnlyEvents that has exactly
        // repeated - apart from timestamps - since the last event of
        // any kind that clears the previous_text_events cache. The
        // idea is to catch tight loops of exception escalation, where
        // the trace shows the (perhaps emulated) CPU getting stuck in
        // its own internal exception logic and never managing to get
        // back to fetching an instruction at all. (An example from a
        // Fast Model is in tests/truncate/crash32m.tarmac .)
        //
        // To do this we use two iterators 'tortoise' and 'hare', with
        // the hare moving 2 spaces per iteration and the tortoise 1,
        // and at each step we compare the intervals [start,tortoise)
        // and [tortoise,hare).
        //
        // To avoid some known false positive cases from real Tarmac
        // producers in which a small number of lines are repeated
        // twice and then sensible output resumes, we insist on the
        // total amount of repeated text being at least a certain size.
        auto tortoise = previous_text_events.rbegin(),
             hare = previous_text_events.rbegin();
        size_t repeated_len = 0;
        while (hare != previous_text_events.rend() &&
               ++hare != previous_text_events.rend() &&
               ++hare != previous_text_events.rend()) {
            ++tortoise;
            repeated_len += 2;
            auto p = previous_text_events.rbegin(), q = tortoise;
            while (q != hare) {
                if (!p->equal_apart_from_timestamp(*q))
                    goto no_match;
                ++p;
                ++q;
            }
            if (repeated_len >= text_event_loop_limit/2) {
                still_reading = false;
                return;
            }
        no_match:;
        }
    }

  public:
    Reader(istream &is, ostream &os, string tarmac_filename,
           const ParseParams &pparams)
        : is(is), os(os), tarmac_filename(tarmac_filename),
          parser(pparams, *this)
    {
    }

    bool read_one_trace_line()
    {
        lineno++;

        string line;
        if (!getline(is, line))
            return false;

        try {
            parser.parse(line);
        } catch (TarmacParseError e) {
            if (is.eof()) {
                ostringstream oss;
                oss << e.msg << endl
                    << _("ignoring parse error on partial last line "
                         "(trace truncated?)");
                reporter->indexing_warning(tarmac_filename, lineno, oss.str());
                return false;
            } else {
                reporter->indexing_error(tarmac_filename, lineno, e.msg);
            }
        }

        os << line << "\n";

        return still_reading;
    }
};

} // namespace

int main(int argc, char **argv)
{
    gettext_setup(true);

    string output_filename("-");

    Argparse ap("tarmac-truncate", argc, argv);
    TarmacUtilityNoIndex tu;
    tu.cannot_use_image();
    tu.add_options(ap);

    ap.optval({"-o", "--output"}, _("FILE"),
              _("file to write output to (default: standard output)"),
              [&](const string &s) { output_filename = s; });

    ap.parse();
    tu.setup();

    unique_ptr<ifstream> ifs;
    istream *isp;
    string input_filename;
    if (tu.tarmac_filename != "-") {
        ifs = make_unique<ifstream>(tu.tarmac_filename.c_str());
        if (ifs->fail())
            reporter->errx(1, _("unable to open input file '%s'"),
                           tu.tarmac_filename.c_str());
        isp = ifs.get();
        input_filename = tu.tarmac_filename;
    } else {
        isp = &cin;
        input_filename = "<standard input>";
    }

    unique_ptr<ofstream> ofs;
    ostream *osp;
    if (output_filename != "-") {
        ofs = make_unique<ofstream>(output_filename.c_str());
        if (ofs->fail())
            reporter->errx(1, _("unable to open output file '%s'"),
                           output_filename.c_str());
        osp = ofs.get();
    } else {
        osp = &cout;
    }

    Reader rdr(*isp, *osp, input_filename, tu.get_parse_params());
    while (rdr.read_one_trace_line())
        ;

    return 0;
}
