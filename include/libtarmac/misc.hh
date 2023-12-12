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

#ifndef LIBTARMAC_MISC_HH
#define LIBTARMAC_MISC_HH

#include <cstddef>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <time.h>

template <typename T, size_t n> static inline void enforce_array(T (&)[n]) {}
#define lenof(x) (sizeof(enforce_array(x), x) / sizeof(*(x)))

typedef unsigned long long Time;
typedef unsigned long long Addr;

template <typename value> inline value absdiff(value a, value b)
{
    return a < b ? b - a : a - b;
}

// Force a string to exactly the given length, padding it with
// 'padvalue' if it's too short.
std::string rpad(const std::string &s, size_t len, char padvalue = ' ');

// Extend 'typ' to at least the same length as 'str', by appending
// copies of padvalue to it.
void type_extend(std::string &typ, const std::string &str, char padvalue);

std::string float_btod(unsigned val);
std::string double_btod(unsigned long long val);

constexpr unsigned long long KNOWN_INVALID_PC = 2;

// A class that provides a cmp() method (used by this library's tree
// searching APIs) to compare it with a type of your choice, and which
// always compares larger / smaller (at your option) than any value of
// that type.
template <class Payload> class Infinity {
    int sign;

  public:
    Infinity(int sign) : sign(sign) {}
    int cmp(const Payload &) const { return sign; }
};

// In the per-platform source
bool get_file_timestamp(const std::string &filename, uint64_t *out_timestamp);
bool is_interactive();
std::string get_error_message();

FILE *fopen_wrapper(const char *filename, const char *mode);
struct tm localtime_wrapper(time_t t);
std::string asctime_wrapper(struct tm tm);

bool get_conf_path(const std::string &filename, std::string &out);

bool get_environment_variable(const std::string &varname, std::string &out);

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#endif // LIBTARMAC_MISC_HH
