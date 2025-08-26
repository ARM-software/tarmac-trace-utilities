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
#include "libtarmac/misc.hh"
#include "libtarmac/image.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/reporter.hh"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using std::cout;
using std::unique_ptr;
using std::string;

unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char *argv[])
{
    bool verbose = false;
    enum Action {
        DEBUG_DUMP,
        FIND_SYMBOL_BY_ADDR,
        FIND_SYMBOL_BY_NAME
    } action = DEBUG_DUMP;
    Addr symbol_addr;
    string symbol_name;
    string image_filename;

    Argparse ap("imagetest", argc, argv);
    ap.optnoval({"-v", "--verbose"}, "print verbose diagnostics during tests",
                [&]() { verbose = true; });
    ap.optval({"-a", "--symbol-addr"}, "SYMBOL_ADDR",
              "print details of a symbol searched by its address",
              [&](const string &arg) {
                  action = FIND_SYMBOL_BY_ADDR;
                  symbol_addr = std::stoul(arg, nullptr, 0);
              });
    ap.optval({"-s", "--symbol-name"}, "SYMBOL_NAME",
              "print details of a symbol searched by its name",
              [&](const string &arg) {
                  action = FIND_SYMBOL_BY_NAME;
                  symbol_name = arg;
              });
    ap.positional("image", "ELF image file to examine",
                  [&](const std::string &arg) { image_filename = arg; });
    ap.parse();

    Image image(image_filename);

    switch (action) {
    case DEBUG_DUMP:
        image.dump();
        return EXIT_SUCCESS;
    case FIND_SYMBOL_BY_ADDR:
        if (const Symbol *sym = image.find_symbol(symbol_addr)) {
            cout << "Symbol at address 0x" << std::hex << symbol_addr << ": '"
                 << sym->getName() << " [0x" << sym->addr << ", 0x"
                 << (sym->addr + sym->size) << ") (" << std::dec << sym->size
                 << " bytes)\n";
            return EXIT_SUCCESS;
        } else {
            cout << "No symbol found at address 0x" << std::hex << symbol_addr
                 << "\n";
            return EXIT_FAILURE;
        }
    case FIND_SYMBOL_BY_NAME:
        if (const Symbol *sym = image.find_symbol(symbol_name)) {
            cout << "Symbol '" << sym->getName() << "' found at address 0x"
                 << std::hex << sym->addr << " (" << std::dec << sym->size
                 << " bytes)\n";
            return EXIT_SUCCESS;
        } else {
            cout << "No symbol found with name '" << symbol_name << "'\n";
            return EXIT_FAILURE;
        }
    }
}
