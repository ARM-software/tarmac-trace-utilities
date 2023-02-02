/*
 * Copyright 2023 Arm Limited. All rights reserved.
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

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::ifstream;

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    bool switchSeen = false;
    string value = "<no value>";
    string arg = "<no arg>";
    vector<string> multis;

    Argparse ap("argparsetest", argc, argv);
    ap.optnoval({"-s", "--switch"}, "option without a value",
                [&]() { switchSeen = true; });
    ap.optval({"-v", "--value"}, "VALUE", "option with a value",
              [&](const string &s) { value = s; });
    ap.optval({"--via-file"}, "FILE", "supply command line option via FILE",
              [&](const string &via) {
                  ifstream f(via.c_str());
                  vector<string> words;
                  while (!f.eof()) {
                      string word;
                      f >> word;
                      words.push_back(word);
                  }
                  while (!words.empty()) {
                      ap.prepend_cmdline_word(words.back());
                      words.pop_back();
                  }
              });
    ap.positional("POS1", "first positional argument",
                  [&](const string &s) { arg = s; });
    ap.positional_multiple("REST", "rest of positional arguments",
                           [&](const string &s) { multis.push_back(s); });
    ap.parse();

    cout << "switchSeen = " << switchSeen << endl
         << "value = " << value << endl
         << "arg = " << arg << endl
         << "multis = [";
    for (auto &s : multis)
        cout << " '" << s << "'";
    cout << " ]" << endl;
}