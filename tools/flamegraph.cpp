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

#include "calltree.hh"

#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using std::cout;
using std::make_unique;
using std::ofstream;
using std::ostream;
using std::string;
using std::unique_ptr;

unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    unique_ptr<string> outfile = nullptr;

    Argparse ap("tarmac-flamegraph", argc, argv);
    TarmacUtility tu(ap);
    ap.optval({"-o", "--output"}, "OUTFILE",
              "write output to OUTFILE "
              "(default: standard output)",
              [&](const string &s) { outfile = make_unique<string>(s); });
    ap.parse();
    tu.setup();

    IndexNavigator IN(tu.trace, tu.image_filename);
    CallTree CT(IN);

    unique_ptr<ofstream> ofs;
    ostream *osp;
    if (outfile) {
        ofs = make_unique<ofstream>(outfile->c_str());
        osp = ofs.get();
    } else {
        osp = &cout;
    }

    CT.generate_flame_graph(*osp);
}
