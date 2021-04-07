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

#include "libtarmac/parser.hh"
#include "libtarmac/argparse.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/registers.hh"

#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using std::cin;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::ifstream;
using std::istream;
using std::make_unique;
using std::ofstream;
using std::ostream;
using std::string;
using std::unique_ptr;
using std::vector;

static bool bigend = false;

class TestReceiver : public ParseReceiver {
    ostream &os;

  public:
    TestReceiver(ostream &os) : os(os) {}

    void got_event(RegisterEvent &ev)
    {
        os << "* RegisterEvent"
           << " time=" << ev.time << " reg=" << ev.reg << hex;
        if (ev.got_value) {
            os << " value=" << ev.value;
        } else {
            os << " bytes=";
            char buf[3];
            const char *sep = "";
            for (uint8_t b : ev.bytes) {
                sprintf(buf, "%02x", (unsigned)b);
                os << sep << buf;
                sep = ":";
            }
        }
        os << dec << endl;
    }

    void got_event(MemoryEvent &ev)
    {
        os << "* MemoryEvent"
           << " time=" << ev.time << " read=" << (ev.read ? "true" : "false")
           << " known=" << (ev.known ? "true" : "false") << " addr=" << hex
           << ev.addr << dec << " size=" << ev.size << " contents=" << hex
           << ev.contents << dec << endl;
    }

    void got_event(InstructionEvent &ev)
    {
        os << "* InstructionEvent"
           << " time=" << ev.time
           << " executed=" << (ev.executed ? "true" : "false") << " pc=" << hex
           << ev.pc << dec << " iset="
           << (ev.iset == ARM
                   ? "ARM"
                   : ev.iset == THUMB ? "Thumb" : ev.iset == A64 ? "A64" : "!!")
           << " width=" << ev.width << " instruction=" << hex << ev.instruction
           << dec << " disassembly=\"" << ev.disassembly << "\"" << endl;
    }

    void got_event(TextOnlyEvent &ev)
    {
        os << "* TextOnlyEvent"
           << " time=" << ev.time << " type=\"" << ev.type << "\""
           << " text=\"" << ev.msg << "\"" << endl;
    }

    bool parse_warning(const string &msg)
    {
        os << "Parse warning: " << msg << endl;
        return false;
    }
};

void run_tests(istream &is, ostream &os)
{
    string line;
    TestReceiver testrecv(os);
    TarmacLineParser parser(bigend, testrecv);

    while (getline(is, line)) {
        if (line.size() == 0 || line[0] == '#')
            continue; // blank line or comment in the input
        os << "--- Tarmac line: " << line << endl;
        try {
            parser.parse(line);
        } catch (TarmacParseError err) {
            os << "Parse error: " << err.msg << endl;
        }
    }
}

class HighlightReceiver : public ParseReceiver {
    string line;
    vector<HighlightClass> highlights;
    bool non_executed_instruction;

  public:
    void init(const string &line_)
    {
        line = line_;
        non_executed_instruction = false;
        highlights.resize(0);
        highlights.resize(line.size() + 1, HL_NONE);
    }
    void highlight(size_t start, size_t end, HighlightClass hc)
    {
        for (size_t i = start; i < end; i++)
            highlights[i] = hc;
    }
    void got_event(InstructionEvent &ev)
    {
        if (!ev.executed)
            non_executed_instruction = true;
    }
    string escape_sequence(HighlightClass hc)
    {
        switch (hc) {
        case HL_NONE:
            return "\033[0;39m";
        case HL_TIMESTAMP:
            return "\033[0;32m";
        case HL_EVENT:
            return "\033[0;1;39m";
        case HL_PC:
            return "\033[1;36m";
        case HL_INSTRUCTION:
            return "\033[0;1;35m";
        case HL_ISET:
            return "\033[0;35m";
        case HL_CPUMODE:
            return "\033[0;36m";
        case HL_CCFAIL:
            return "\033[0;31m";
        case HL_DISASSEMBLY:
            return non_executed_instruction ? "\033[0;31m" : "\033[0;1;32m";
        case HL_TEXT_EVENT:
            return "\033[0;39m";
        case HL_PUNCT:
            return "\033[0;33m";
        case HL_ERROR:
            return "\033[0;1;41;33m";
        default:
            assert(false && "unknown class");
            return "";
        }
    }
    void display(ostream &os)
    {
        HighlightClass curr_class = HL_NONE;
        for (size_t i = 0; i <= line.size(); i++) {
            HighlightClass next_class = highlights[i];
            if (next_class != curr_class) {
                os << escape_sequence(next_class);
                curr_class = next_class;
            }
            if (i == line.size())
                break;
            os << line[i];
        }
        os << endl;
    }
};

void syntax_highlight(istream &is, ostream &os)
{
    string line;
    HighlightReceiver recv;
    TarmacLineParser parser(bigend, recv);

    while (getline(is, line)) {
        recv.init(line);
        try {
            parser.parse(line);
        } catch (TarmacParseError err) {
        }
        recv.display(os);
    }
}

int main(int argc, char **argv)
{
    void (*do_stuff)(istream &, ostream &) = run_tests;
    unique_ptr<string> infile = nullptr, outfile = nullptr;

    Argparse ap("parsertest", argc, argv);
    ap.optnoval({"--highlight"}, "syntax-highlight the Tarmac input",
                [&]() { do_stuff = syntax_highlight; });
    ap.optval({"-o", "--output"}, "OUTFILE",
              "write output to OUTFILE "
              "(default: standard output)",
              [&](const string &s) { outfile = make_unique<string>(s); });
    ap.optnoval({"--li"}, "put parser in little-endian mode",
                []() { bigend = false; });
    ap.optnoval({"--bi"}, "put parser in big-endian mode",
                []() { bigend = true; });
    ap.positional("INFILE", "input file to parse (default: standard input)",
                  [&](const string &s) { infile = make_unique<string>(s); },
                  false /* not required */);
    ap.parse();

    unique_ptr<ifstream> ifs;
    istream *isp;
    if (infile) {
        ifs = make_unique<ifstream>(infile->c_str());
        isp = ifs.get();
    } else {
        isp = &cin;
    }

    unique_ptr<ofstream> ofs;
    ostream *osp;
    if (outfile) {
        ofs = make_unique<ofstream>(outfile->c_str());
        osp = ofs.get();
    } else {
        osp = &cout;
    }

    do_stuff(*isp, *osp);

    return 0;
}
