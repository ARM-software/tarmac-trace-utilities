/*
 * Copyright 2016-2021,2026 Arm Limited. All rights reserved.
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

#include "libtarmac/json.hh"

#include <assert.h>
#include <iomanip>
#include <ostream>

using std::hex;
using std::ios;
using std::ostream;
using std::setfill;
using std::setw;
using std::string;

JsonWriter::JsonWriter(ostream &os, bool pretty, unsigned indent_width)
    : os(os), pretty(pretty), indent_width(indent_width), stack()
{
}

void JsonWriter::write_indent() const
{
    if (pretty)
        os << string(stack.size() * indent_width, ' ');
}

void JsonWriter::write_escaped_string(const string &value)
{
    os << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            os << "\\\\";
            break;
        case '"':
            os << "\\\"";
            break;
        case '\b':
            os << "\\b";
            break;
        case '\f':
            os << "\\f";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\t':
            os << "\\t";
            break;
        default:
            if (ch < 0x20) {
                ios::fmtflags saved_flags = os.flags();
                char saved_fill = os.fill();
                os << "\\u" << hex << setw(4) << setfill('0')
                   << static_cast<unsigned>(ch);
                os.flags(saved_flags);
                os.fill(saved_fill);
            } else {
                os << static_cast<char>(ch);
            }
            break;
        }
    }
    os << '"';
}

void JsonWriter::prefix_value()
{
    if (!stack.empty()) {
        assert(stack.back().context == Context::Array);
        if (!stack.back().first_entry)
            os << ',';
        if (pretty)
            os << '\n';
        write_indent();
        stack.back().first_entry = false;
    }
}

void JsonWriter::prefix_field(const string &name)
{
    assert(!stack.empty());
    assert(stack.back().context == Context::Object);
    if (!stack.back().first_entry)
        os << ',';
    if (pretty)
        os << '\n';
    write_indent();
    write_escaped_string(name);
    os << ':';
    if (pretty)
        os << ' ';
    stack.back().first_entry = false;
}

void JsonWriter::open_value(char ch)
{
    os << ch;
}

void JsonWriter::close_value(char ch, Context expected_context)
{
    assert(!stack.empty());
    assert(stack.back().context == expected_context);
    if (pretty && !stack.back().first_entry) {
        os << '\n';
        string indent((stack.size() - 1) * indent_width, ' ');
        os << indent;
    }
    stack.pop_back();
    os << ch;
}

void JsonWriter::obj_open()
{
    prefix_value();
    open_value('{');
    stack.push_back({Context::Object, true});
}

void JsonWriter::obj_close()
{
    close_value('}', Context::Object);
}

void JsonWriter::array_open()
{
    prefix_value();
    open_value('[');
    stack.push_back({Context::Array, true});
}

void JsonWriter::array_close()
{
    close_value(']', Context::Array);
}

void JsonWriter::obj_field_string(const string &name, const string &value)
{
    prefix_field(name);
    write_escaped_string(value);
}

void JsonWriter::obj_field_number(const string &name, uint64_t value)
{
    prefix_field(name);
    os << value;
}

void JsonWriter::obj_field_bool(const string &name, bool value)
{
    prefix_field(name);
    os << (value ? "true" : "false");
}

void JsonWriter::obj_field_null(const string &name)
{
    prefix_field(name);
    os << "null";
}

void JsonWriter::obj_field_obj_open(const string &name)
{
    prefix_field(name);
    open_value('{');
    stack.push_back({Context::Object, true});
}

void JsonWriter::obj_field_array_open(const string &name)
{
    prefix_field(name);
    open_value('[');
    stack.push_back({Context::Array, true});
}

void JsonWriter::array_entry_string(const string &value)
{
    prefix_value();
    write_escaped_string(value);
}

void JsonWriter::array_entry_number(uint64_t value)
{
    prefix_value();
    os << value;
}

void JsonWriter::array_entry_bool(bool value)
{
    prefix_value();
    os << (value ? "true" : "false");
}

void JsonWriter::array_entry_null()
{
    prefix_value();
    os << "null";
}

void JsonWriter::array_entry_obj_open()
{
    prefix_value();
    open_value('{');
    stack.push_back({Context::Object, true});
}

void JsonWriter::array_entry_array_open()
{
    prefix_value();
    open_value('[');
    stack.push_back({Context::Array, true});
}
