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

#include "libtarmac/misc.hh"

#include <algorithm>
#include <assert.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using std::min;
using std::ostringstream;
using std::string;
using std::unique_ptr;
using std::vector;

string format_internal(const string &fmt,
                       const vector<std::function<void(std::ostream &)>> &args)
{
    ostringstream oss;
    bool used_implicit = false, used_explicit = false;
    size_t next_implicit = 0;

    for (size_t pos = 0, end = fmt.size(); pos < end;) {
        char c = fmt[pos];
        if (c != '{' && c != '}') {
            oss << c;
            ++pos;
        } else if (pos < end - 1 && fmt[pos + 1] == c) {
            oss << c;
            pos += 2;
        } else {
            assert(c == '{' && "Stray unduplicated } in format string");
            size_t dstart = ++pos;
            while (pos < end && fmt[pos] != '}')
                ++pos;
            assert(pos < end && "Unterminated format directive");
            size_t dend = pos++;

            string d = fmt.substr(dstart, dend - dstart);
            size_t index;

            size_t colon = d.find(':');
            if (colon == string::npos)
                colon = d.size();
            string arg_id = d.substr(0, colon);
            string format_type = d.substr(min(colon + 1, d.size()));

            if (arg_id.empty()) {
                assert(!used_explicit &&
                       "Can't mix implicit and explicit argument positions");
                used_implicit = true;
                index = next_implicit++;
            } else {
                assert(!used_implicit &&
                       "Can't mix implicit and explicit argument positions");
                used_explicit = true;
                assert(arg_id.find_first_not_of("0123456789") == string::npos &&
                       "Bad argument index");
                index = std::stoul(arg_id, nullptr, 10);
            }

            assert(index < args.size() && "Index out of range");

            ostringstream tmp;
            if (format_type == "x") {
                tmp << std::hex;
            } else if (format_type == "#x") {
                tmp << std::hex << std::showbase;
            } else {
                assert(format_type.size() == 0 &&
                       "Unsupported format directive");
            }
            args[index](tmp);
            oss << tmp.str();
        }
    }

    // Suppress -Wunused-but-set-variable warnings when assertions compiled out
    (void)used_implicit;
    (void)used_explicit;

    return oss.str();
}
