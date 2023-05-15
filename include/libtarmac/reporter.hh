/*
 * Copyright 2021 Arm Limited. All rights reserved.
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

#ifndef LIBTARMAC_REPORTER_HH
#define LIBTARMAC_REPORTER_HH

#include <memory>
#include <ostream>
#include <string>

// Enumeration used by Reporter::indexing_status() below.
enum class IndexUpdateCheck {
    OK,             // no rebuild needed
    Missing,        // rebuild needed: index not present
    TooOld,         // rebuild needed: index older than trace file
    WrongFormat,    // rebuild needed: index has wrong file format version
    Incomplete,     // rebuild needed: previous generation did not finish
    Forced,         // rebuild explicitly requested by user
};

/*
 * A Reporter is an object that knows how to display diagnostics to
 * the user, and perhaps exit in the case where diagnostics are fatal.
 *
 * It's concealed behind an abstract base class so that the
 * implementation can be swapped out between GUI and command-line
 * tools. (On some GUI platforms, you can't print the progress of
 * indexing to standard error, because you don't _have_ a standard
 * error. So you have to do it with a GUI progress-bar dialog instead.
 * Similarly for fatal error messages.)
 */
class Reporter {
  protected:
    bool verbose = false, progress = false;

  public:
    virtual ~Reporter() = default;

    // Report warnings and fatal errors.
    //
    // Semantics are similar to the BSDish <err.h>. The err and errx
    // functions are fatal errors, which terminate the program with
    // the provided 'exitstatus' and do not return. warn and warnx
    // print a message and continue.
    //
    // err and warn suffix strerror(errno) to the message, for
    // reporting OS-level errors; errx and warnx do not.
    [[noreturn]] virtual void err(int exitstatus, const char *fmt, ...) = 0;
    [[noreturn]] virtual void errx(int exitstatus, const char *fmt, ...) = 0;
    virtual void warn(const char *fmt, ...) = 0;
    virtual void warnx(const char *fmt, ...) = 0;

    void set_indexing_verbosity(bool val) { verbose = val; }

    // Announce the status of an index update check (i.e. whether an
    // index needs to be created, or whether it already exists and
    // appears up to date).
    virtual void indexing_status(const std::string &index_filename,
                                 const std::string &trace_filename,
                                 IndexUpdateCheck status) = 0;

    // Report a warning or fatal error during indexing, such as a
    // parsing problem. indexing_error does not return.
    virtual void indexing_warning(const std::string &trace_filename,
                                  unsigned lineno, const std::string &msg) = 0;
    virtual void indexing_error(const std::string &trace_filename,
                                unsigned lineno, const std::string &msg) = 0;

    void set_indexing_progress(bool val) { progress = val; }

    // Report progress during file indexing.
    virtual void indexing_start(std::streampos total) = 0;
    virtual void indexing_progress(std::streampos pos) = 0;
    virtual void indexing_done() = 0;
};

std::unique_ptr<Reporter> make_cli_reporter();

/*
 * We don't expect different parts of the same process to need
 * different Reporter classes, so there's no need to pass a Reporter
 * pointer around all over the place. Instead, there's a single global
 * instance, and each application initializes it before doing anything.
 */
extern std::unique_ptr<Reporter> reporter;

#endif // LIBTARMAC_REPORTER_HH
