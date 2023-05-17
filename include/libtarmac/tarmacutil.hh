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

#ifndef TARMAC_MAIN_COMMON_HH
#define TARMAC_MAIN_COMMON_HH

#include "libtarmac/argparse.hh"
#include "libtarmac/misc.hh"

#include <string>
#include <vector>

class TarmacUtilityBase {
  public:
    TarmacUtilityBase();

    // Add command-line options to the provided Argparse object. A
    // standard set of these is provided; derived classes can override
    // this to add further options (but should call the base class
    // version).
    virtual void add_options(Argparse &ap);

    // After calling Argparse::parse(), this function will do the
    // tool's standard initial setup, building or rebuilding the index
    // file if needed. If you gave the --only-index option, it will
    // exit.
    void setup();

    // This is like setup(), but doesn't exit in only-index mode. You
    // can use the only_index() query afterwards to find out if the
    // user asked you not to do anything else.
    void setup_noexit();

    bool only_index() const { return onlyIndex; }
    bool is_verbose() const { return verbose; }

    // Functions that clients can call before add_options(), to signal
    // which functionality is present and/or relevant in this tool, so
    // as to control its command-line options and behaviour.
    void cannot_use_image() { can_use_image = false; }

    std::string image_filename;

  protected:
    enum class Troolean { No, Auto, Yes };

    Troolean indexing = Troolean::Auto;
    bool can_use_image = true;
    bool onlyIndex = false;
    bool index_on_disk = true;
    // Marks whether bigend was specified via a parameter
    bool bigend_explicit = false;
    bool bigend = false;
    bool verbose;
    bool show_progress_meter;

    void updateIndexIfNeeded(const TracePair &trace) const;

  private:
    // Subclass-dependent functionality.
    virtual void postProcessOptions() = 0;
    virtual void setupIndex() const = 0;
};

/*
 * Class describing the typical command line of a Tarmac Trace
 * Utilities tool, which expects a single trace file name on the
 * command line, plus an optional ELF image, and some options to force
 * or suppress re-indexing.
 */
struct TarmacUtility : public TarmacUtilityBase {
    TracePair trace;

    bool trace_required = true;

    void trace_argument_optional() { trace_required = false; }

    virtual void add_options(Argparse &ap) override;
    virtual void postProcessOptions() override;
    virtual void setupIndex() const override
    {
        updateIndexIfNeeded(trace);
    }
};

/*
 * Class describing a variant command line syntax in which multiple
 * trace files are accepted on a single command line.
 */
struct TarmacUtilityMT : public TarmacUtilityBase {
    std::vector<TracePair> traces;

    virtual void add_options(Argparse &ap) override;
    virtual void postProcessOptions() override {}
    virtual void setupIndex() const override
    {
        for (const TracePair &trace : traces)
            updateIndexIfNeeded(trace);
    }
};

#endif // TARMAC_MAIN_COMMON_HH
