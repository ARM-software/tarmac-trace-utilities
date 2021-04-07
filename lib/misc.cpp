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

#include "libtarmac/misc.hh"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

using std::string;

string rpad(const string &s, size_t len, char padvalue)
{
    if (s.size() < len) {
        string padding(len - (int)s.size(), padvalue);
        return s + padding;
    } else {
        return s.substr(0, len);
    }
}

void type_extend(string &typ, const string &str, char padvalue)
{
    if (typ.size() < str.size())
        typ += string(str.size() - typ.size(), padvalue);
}

void err(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::string errmsg = get_error_message();
    fprintf(stderr, ": %s\n", errmsg.c_str());
    exit(exitstatus);
}

void errx(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
    exit(exitstatus);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::string errmsg = get_error_message();
    fprintf(stderr, ": %s\n", errmsg.c_str());
}

void warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}
