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

#include "callinfo.hh"
#include "libtarmac/argparse.hh"
#include "libtarmac/index.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <string>
#include <vector>

using std::string;
using std::vector;

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    vector<string> functions;

    Argparse ap("tarmac-callinfo", argc, argv);
    TarmacUtility tu(ap);

    ap.positional_multiple("FUNCTION",
                           "name or hex address of function to "
                           "find calls to",
                           [&](const string &s) { functions.push_back(s); });

    ap.parse([&]() {
        if (functions.empty())
            throw ArgparseError("expected at least one function name");
    });
    tu.setup();

    CallInfo CI(tu.trace, tu.image_filename);
    CI.run(functions);

    return 0;
}
