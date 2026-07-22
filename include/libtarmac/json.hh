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

#ifndef LIBTARMAC_JSON_HH
#define LIBTARMAC_JSON_HH

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class JsonWriter {
    enum class Context { Object, Array };
    struct Frame {
        Context context;
        bool first_entry;
    };

    std::ostream &os;
    bool pretty;
    unsigned indent_width;
    std::vector<Frame> stack;

    void prefix_value();
    void prefix_field(const std::string &name);
    void write_indent() const;
    void write_escaped_string(const std::string &value);
    void open_value(char ch);
    void close_value(char ch, Context expected_context);

  public:
    explicit JsonWriter(std::ostream &os, bool pretty = true,
                        unsigned indent_width = 2);

    void obj_open();
    void obj_close();
    void array_open();
    void array_close();

    void obj_field_string(const std::string &name, const std::string &value);
    void obj_field_number(const std::string &name, uint64_t value);
    void obj_field_bool(const std::string &name, bool value);
    void obj_field_null(const std::string &name);
    void obj_field_obj_open(const std::string &name);
    void obj_field_array_open(const std::string &name);

    void array_entry_string(const std::string &value);
    void array_entry_number(uint64_t value);
    void array_entry_bool(bool value);
    void array_entry_null();
    void array_entry_obj_open();
    void array_entry_array_open();
};

#endif // LIBTARMAC_JSON_HH
