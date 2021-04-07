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
#include <string>

template <typename T, size_t n> static inline void enforce_array(T (&)[n]) {}
#define lenof(x) (sizeof(enforce_array(x), x) / sizeof(*(x)))

typedef unsigned Time;
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

// A tuple class that encapsulates the filenames of a Tarmac trace
// file and its associated index.
struct TracePair {
    std::string tarmac_filename;
    std::string index_filename;

    TracePair() = default;
    TracePair(const std::string &tarmac_filename,
              const std::string &index_filename)
        : tarmac_filename(tarmac_filename), index_filename(index_filename)
    {
    }
};

// Similar to the BSDish <err.h>: err and warn suffix strerror(errno)
// to the message, whereas errx and warnx do not
void err(int exitstatus, const char *fmt, ...);
void errx(int exitstatus, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);

// In the per-platform source
bool get_file_timestamp(const std::string &filename, uint64_t *out_timestamp);
bool is_interactive();
std::string get_error_message();

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#endif // LIBTARMAC_MISC_HH
