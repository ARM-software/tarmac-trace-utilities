/*
 * Copyright 2026 Arm Limited. All rights reserved.
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
#include "libtarmac/calltree.hh"
#include "libtarmac/intl.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

using std::cout;
using std::make_unique;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::string;
using std::unique_ptr;

unique_ptr<Reporter> reporter = make_cli_reporter();

namespace {

class ChromeTraceJson {
  public:
    ChromeTraceJson(ostream &os) : os(os), first_event(true) {}

    void start() { os << "{\n  \"traceEvents\": [\n"; }

    void finish() { os << "\n  ]\n}\n"; }

    void metadata_event(const string &name, const string &value, int pid,
                        int tid)
    {
        separator();
        os << "    {\"ph\":\"M\",\"name\":\"" << json_escape(name)
           << "\",\"pid\":" << pid << ",\"tid\":" << tid
           << ",\"args\":{\"name\":\"" << json_escape(value) << "\"}}";
    }

    void function_event(const string &name, int pid, int tid, Time ts, Time dur,
                        Addr addr, unsigned long long line)
    {
        separator();

        string func_name = name;
        if (func_name.empty()) {
            // Fall back to printing address when we have no valid name
            func_name = format_as_address(addr);
        }

        os << "    {\"ph\":\"X\",\"cat\":\"function\",\"name\":\""
           << json_escape(func_name) << "\",\"pid\":" << pid
           << ",\"tid\":" << tid << ",\"ts\":" << ts << ",\"dur\":" << dur
           << ",\"args\":{\"pc\":\"" << format_as_address(addr)
           << "\",\"line\":" << line << "}}";
    }

  private:
    std::ostream &os;
    bool first_event;

    void separator()
    {
        if (first_event) {
            first_event = false;
        } else {
            os << ",\n";
        }
    }

    string format_as_address(Addr addr)
    {
        std::ostringstream oss;
        oss << "0x" << std::hex << addr;
        return oss.str();
    }

    string json_escape(const string &input)
    {
        std::ostringstream oss;
        constexpr unsigned char first_printable_ascii = ' ';

        for (unsigned char c : input) {
            switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\b':
                oss << "\\b";
                break;
            case '\f':
                oss << "\\f";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                // If we get other control bytes, encode as '\uXXXX', where
                // 'XXXX' are hex digits encoding the character. Here we pad
                // with zero to fill the 4 digits
                if (c < first_printable_ascii) {
                    oss << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    oss << c;
                }
                break;
            }
        }

        return oss.str();
    }
};

class ChromeTraceWriter : public CallTreeVisitor {
    ChromeTraceJson &out_json;
    int pid;
    int tid;

  public:
    ChromeTraceWriter(const CallTree &CT, ChromeTraceJson &json, int pid,
                      int tid)
        : CallTreeVisitor(CT), out_json(json), pid(pid), tid(tid)
    {
    }

    void onFunctionEntry(const TarmacSite &function_entry,
                         const TarmacSite &function_exit)
    {
        if (function_exit.time < function_entry.time) {
            return;
        }

        out_json.function_event(
            CT.getFunctionName(function_entry), pid, tid, function_entry.time,
            function_exit.time - function_entry.time, function_entry.addr,
            function_entry.tarmac_line);
    }
};

} // namespace

int main(int argc, char **argv)
{
    gettext_setup(true);

    unique_ptr<string> outfile = nullptr;

    // Default PID and TID to 1
    int pid = 1;
    int tid = 1;

    IndexerParams iparams;
    iparams.record_memory = false;

    CallTreeOptions ctopts;

    Argparse ap("tarmac-chromejson", argc, argv);
    TarmacUtility tu;
    tu.set_indexer_params(iparams);
    tu.add_options(ap);
    ctopts.add_options(ap);
    ap.optval({"-o", "--output"}, _("OUTFILE"),
              _("write output to OUTFILE (default: standard output)"),
              [&](const string &s) { outfile = make_unique<string>(s); });
    ap.optval({"--pid"}, _("PID"), _("process id to write into trace events"),
              [&](const string &s) { pid = stoi(s); });
    ap.optval({"--tid"}, _("TID"), _("thread id to write into trace events"),
              [&](const string &s) { tid = stoi(s); });
    ap.parse();
    tu.setup();

    IndexNavigator IN(tu.trace, tu.image_filename, tu.load_offset);
    CallTree CT(IN);
    CT.setOptions(ctopts);

    unique_ptr<ofstream> ofs;
    ostream *osp;
    if (outfile) {
        ofs = make_unique<ofstream>(outfile->c_str());
        osp = ofs.get();
    } else {
        osp = &cout;
    }

    ChromeTraceJson output_json(*osp);
    output_json.start();
    output_json.metadata_event("process_name", "Tarmac trace", pid, tid);
    output_json.metadata_event("thread_name", "CPU 0", pid, tid);

    ChromeTraceWriter writer(CT, output_json, pid, tid);
    CT.visit(writer);

    output_json.finish();
}
