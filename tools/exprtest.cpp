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

#include "libtarmac/expr.hh"
#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using std::clog;
using std::cout;
using std::endl;
using std::ifstream;
using std::istringstream;
using std::make_unique;
using std::ostringstream;
using std::string;
using std::unique_ptr;

struct TestExecutionContext : ExecutionContext {
    bool lookup(const string &name, Context context, uint64_t &out) const
    {
        out = 12345;
        return true;
    }
};

static void test_parse_expression(const std::string &title,
                                  const std::string &str)
{
    ostringstream err;
    ExprPtr expr = parse_expression(str, err);

    if (!expr) {
        cout << title << ": parse failure: " << err.str() << endl;
        return;
    }

    cout << title << ": parse gives ";
    expr->dump(cout);
    cout << endl;

    TestExecutionContext tec;
    uint64_t val = expr->evaluate(tec);
    cout << title << ": evaluation gives " << val << endl;
}

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    unique_ptr<string> infile = nullptr, expr = nullptr;

    Argparse ap("exprtest", argc, argv);
    ap.optval({"--infile"}, "INFILE",
              "file of test expressions to parse,"
              " one per line",
              [&](const string &s) { infile = make_unique<string>(s); });
    ap.positional("EXPR", "test expression to parse",
                  [&](const string &s) { expr = make_unique<string>(s); },
                  false /* not required */);
    ap.parse([&]() {
        if (!infile && !expr)
            throw ArgparseError("expected either an input file or "
                                "an expression");
        if (infile && expr)
            throw ArgparseError("expected only one of an input file and "
                                "an expression");
    });

    if (infile) {
        ifstream ifs(infile->c_str());
        string line;
        unsigned lineno = 1;

        while (getline(ifs, line)) {
            ostringstream oss;
            oss << "line " << lineno++;
            if (line.empty() || line.substr(0, 1) == "#")
                continue;
            test_parse_expression(oss.str(), line);
        }
    }

    if (expr)
        test_parse_expression("command-line expression", *expr);

    return 0;
}
