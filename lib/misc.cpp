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

#include "libtarmac/intl.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/disktree.hh"
#include "libtarmac/reporter.hh"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <iostream>
#include <sstream>

using std::clog;
using std::endl;
using std::make_unique;
using std::streampos;
using std::string;
using std::unique_ptr;

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

class CommandLineReporter : public Reporter {
    [[noreturn]] void err(int exitstatus, const char *fmt, ...) override;
    [[noreturn]] void errx(int exitstatus, const char *fmt, ...) override;
    void warn(const char *fmt, ...) override;
    void warnx(const char *fmt, ...) override;
    void indexing_status(const TracePair &pair,
                         IndexUpdateCheck status) override;
    void indexing_warning(const string &trace_filename,
                          unsigned lineno, const string &msg) override;
    void indexing_error(const string &trace_filename,
                        unsigned lineno, const string &msg) override;
    void indexing_start(streampos total) override;
    void indexing_progress(streampos pos) override;
    void indexing_done() override;

    streampos indexing_total_size;
    int last_shown_progress_percentage;

  public:
    CommandLineReporter() = default;
};

unique_ptr<Reporter> make_cli_reporter()
{
    return make_unique<CommandLineReporter>();
}

[[noreturn]] void CommandLineReporter::err(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::string errmsg = get_error_message();
    fprintf(stderr, ": %s\n", errmsg.c_str());
    exit(exitstatus);
}

[[noreturn]] void CommandLineReporter::errx(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
    exit(exitstatus);
}

void CommandLineReporter::warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::string errmsg = get_error_message();
    fprintf(stderr, ": %s\n", errmsg.c_str());
}

void CommandLineReporter::warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}

void CommandLineReporter::indexing_status(const TracePair &pair,
                                          IndexUpdateCheck status)
{
    if (!verbose)
        return;

    switch (status) {
      case IndexUpdateCheck::InMemory:
        // If index is in memory, no need to print anything
        break;
      case IndexUpdateCheck::Missing:
          clog << format(_("index file {} does not exist; building it"),
                         pair.index_filename)
               << endl;
          break;
      case IndexUpdateCheck::TooOld:
          clog << format(_("index file {} is older than trace file {}; "
                           "rebuilding it"),
                         pair.index_filename, pair.tarmac_filename)
               << endl;
          break;
      case IndexUpdateCheck::WrongFormat:
          clog << format(_("index file {} was not generated by this version of "
                           "the tool; rebuilding it"),
                         pair.index_filename)
               << endl;
          break;
      case IndexUpdateCheck::Incomplete:
          clog << format(_("previous generation of index file {} was not "
                           "completed; rebuilding it"),
                         pair.index_filename)
               << endl;
          break;
      case IndexUpdateCheck::OK:
          clog << format(_("index file {} looks ok; not rebuilding it"),
                         pair.index_filename)
               << endl;
          break;
      case IndexUpdateCheck::Forced:
        // If the user asked explicitly for a reindex, we don't need
        // to say why we're doing it
        break;
    }
}

void CommandLineReporter::indexing_warning(const string &trace_filename,
                                           unsigned lineno, const string &msg)
{
    clog << trace_filename << ":" << lineno << ": " << msg << endl;
}

void CommandLineReporter::indexing_error(const string &trace_filename,
                                         unsigned lineno, const string &msg)
{
    clog << trace_filename << ":" << lineno << ": " << msg << endl;
    exit(1);
}

void CommandLineReporter::indexing_start(streampos total)
{
    last_shown_progress_percentage = -1;
    indexing_total_size = total;
}

void CommandLineReporter::indexing_progress(streampos pos)
{
    if (!progress)
        return;

    int percentage = 100 * pos / indexing_total_size;
    if (percentage != last_shown_progress_percentage) {
        last_shown_progress_percentage = percentage;
        clog << "\r" << format(_("Reading trace file ({}%)"), percentage);
        clog.flush();
    }
}

void CommandLineReporter::indexing_done()
{
    if (!progress)
        return;

    clog << "\r" << _("Reading trace file (finished)") << endl;
}

OFF_T Arena::alloc(size_t size)
{
    if ((size_t)(curr_size - next_offset) < size) {
        OFF_T new_curr_size = (next_offset + size) * 5 / 4 + 65536;
        assert(new_curr_size >= next_offset);
        resize(new_curr_size);
    }
    OFF_T ret = next_offset;
    next_offset += size;
    return ret;
}

MemArena::~MemArena()
{
    free(mapping);
}

void MemArena::resize(size_t newsize)
{
    mapping = realloc(mapping, newsize);
    if (!mapping)
        reporter->errx(1, _("Out of memory"));
    curr_size = newsize;
}

static std::wstring string_to_wstring(const std::string &str)
{
    std::wostringstream woss;
    mbstate_t state = {0};
    for (char c: str) {
        wchar_t wc;
        if (mbrtowc(&wc, &c, 1, &state) == 1)
            woss << wc;
    }
    return woss.str();
}

size_t terminal_width(const std::string &str)
{
    std::wstring wstr = string_to_wstring(str);
#if HAVE_WCSWIDTH
    return wcswidth(wstr.c_str(), wstr.size());
#else
    // If we don't have wcswidth available, I don't know of any other
    // reliable way to identify double- or zero-width wide characters,
    // so we're just going to have to approximate.
    return wstr.size();
#endif
}
