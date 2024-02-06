/*
 * Copyright 2024 Arm Limited. All rights reserved.
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

/*
 The purpose of this program is to ensure that we can use the
 tarmac-trace-utilities as a downstream project. It does nothing much, apart
 from allowing us to check that :
  - the included headers were found,
  - we were able to link against the tarmac library,
  - it runs just fine.
 */
#include "libtarmac/reporter.hh"
#include "libtarmac/argparse.hh"

#include <cstdlib>
#include <memory>
#include <iostream>

using namespace std;

unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char *argv[])
{
    enum { NOTHING, HELLO } action = NOTHING;
    Argparse ap("ttu-import", argc, argv);
    ap.optnoval({"--hello"}, "Emit a hello message on stdout",
                [&]() { action = HELLO; });
    ap.parse();

    switch (action) {
    case NOTHING:
        break;
    case HELLO:
        cout << "Hello from ttu-import !\n";
        break;
    }

    return EXIT_SUCCESS;
}
