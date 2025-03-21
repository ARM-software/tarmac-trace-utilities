/*
 * Copyright 2016-2023,2025 Arm Limited. All rights reserved.
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

#include "libtarmac/disktree.hh"
#include "libtarmac/tarmacutil.hh"
#include "libtarmac/index.hh"
#include "libtarmac/intl.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/reporter.hh"

#include <iostream>
#include <memory>
#include <stdlib.h>

using std::cout;
using std::make_shared;
using std::string;

TarmacUtilityBase::TarmacUtilityBase()
    : verbose(is_interactive()), show_progress_meter(verbose) {
    idiags.diagnostics_stream = &cout;
}

void TarmacUtilityBase::add_options(Argparse &ap)
{
    if (!iparams.can_store_on_disk())
        index_on_disk = false;

    if (can_use_image) {
        ap.optval({"--image"}, _("IMAGEFILE"), _("image file name"),
                  [this](const string &s) { image_filename = s; });
        ap.optval({"--load-offset"}, _("OFFSET"), _("offset from addresses in "
                  "the image file to addresses in the trace"),
                  [this](const string &s) {
                      load_offset = stoull(s, nullptr, 0);
                  });
    }
    if (index_on_disk) {
        ap.optnoval({"--only-index"}, _("generate index and do nothing else"),
                    [this]() {
                        indexing = Troolean::Yes;
                        onlyIndex = true;
                    });
        ap.optnoval({"--force-index"}, _("regenerate index unconditionally"),
                    [this]() { indexing = Troolean::Yes; });
        ap.optnoval({"--no-index"}, _("do not regenerate index"),
                    [this]() { indexing = Troolean::No; });
        ap.optnoval({"--memory-index"},
                    _("keep index in memory instead of on disk"),
                    [this]() { index_on_disk = false; });
    }
    ap.optnoval({"--li"}, _("assume trace is from a little-endian platform"),
                [this]() {
                    bigend = false;
                    bigend_explicit = true;
                });
    ap.optnoval({"--bi"}, _("assume trace is from a big-endian platform"),
                [this]() {
                    bigend = true;
                    bigend_explicit = true;
                });
    ap.optnoval({"--implicit-thumb"}, _("assume trace is from a Thumb-only "
                "platform and might omit the instruction set state from "
                "trace records"),
                [this]() {
                    thumbonly = true;
                });
    ap.optval({"--debug"}, _("TYPE"), _("enable diagnostics of type TYPE "
                "(use --debug=list for a list)"),
                [this](const string &s) {
                    if (s == "list") {
                        cout << _("List of diagnostic types:") << "\n"
                             << "--debug=call_heuristics: "
                             << _("debug call and return analysis") << "\n";
                    } else if (s == "call_heuristics") {
                        idiags.debug_call_heuristics = true;
                    } else {
                        throw ArgparseError(
                            format(_("unknown diagnostic type '{}'"), s));
                    }
                });
    ap.optnoval({"-v", "--verbose"}, _("make tool more verbose"),
                [this]() { verbose = true; });
    ap.optnoval({"-q", "--quiet"}, _("make tool quiet"),
                [this]() { verbose = show_progress_meter = false; });
    ap.optnoval({"--show-progress-meter"},
                _("force display of the progress meter"),
                [this]() { show_progress_meter = true; });
}

void TarmacUtility::add_options(Argparse &ap)
{
    TarmacUtilityBase::add_options(ap);

    ap.optval({"--index"}, _("INDEXFILE"), _("index file name"),
              [this](const string &s) { trace.index_filename = s; });
    ap.positional(_("TRACEFILE"), _("Tarmac trace file to read"),
                  [this](const string &s) { trace.tarmac_filename = s; },
                  trace_required);
}

static string defaultIndexFilename(string tarmac_filename)
{
    return tarmac_filename + ".index";
}

void TarmacUtility::postProcessOptions()
{
    trace.index_on_disk = index_on_disk;
    if (index_on_disk) {
        if (trace.index_filename.empty())
            trace.index_filename = defaultIndexFilename(trace.tarmac_filename);
    } else {
        if (indexing == Troolean::No)
            reporter->warnx(_("Ignoring --no-index since index is in memory"));
        if (!trace.index_filename.empty())
            reporter->warnx(_("Ignoring index file name since index is "
                              "in memory"));
        indexing = Troolean::Yes;
        trace.memory_index = make_shared<MemArena>();
    }

    load_image();
}

void TarmacUtilityMT::add_options(Argparse &ap)
{
    TarmacUtilityBase::add_options(ap);

    auto add_pair = [this](const string &s) {
        TracePair pair;
        pair.tarmac_filename = s;
        pair.index_on_disk = index_on_disk;
        if (index_on_disk)
            pair.index_filename = defaultIndexFilename(s);
        traces.push_back(pair);
    };
    ap.positional_multiple(_("TRACEFILE"), _("Tarmac trace files to read"),
                           add_pair);
}

void TarmacUtilityBase::updateIndexIfNeeded(const TracePair &trace) const
{
    Troolean doIndexing = indexing; // so we can translate Auto into Yes or No

    reporter->set_indexing_verbosity(verbose);
    reporter->set_indexing_progress(show_progress_meter);

    if (!trace.index_on_disk) {
        // If we're indexing to memory, there can never be an existing index
        doIndexing = Troolean::Yes;
        reporter->indexing_status(trace, IndexUpdateCheck::InMemory);
    } else if (doIndexing == Troolean::Auto) {
        uint64_t trace_timestamp, index_timestamp;
        IndexUpdateCheck status;

        if (!get_file_timestamp(trace.tarmac_filename, &trace_timestamp))
            reporter->err(1, "%s: stat", trace.tarmac_filename.c_str());

        if (!get_file_timestamp(trace.index_filename, &index_timestamp)) {
            status = IndexUpdateCheck::Missing;
        } else if (index_timestamp < trace_timestamp) {
            status = IndexUpdateCheck::TooOld;
        } else {
            switch (check_index_header(trace.index_filename)) {
            case IndexHeaderState::WrongMagic:
                status = IndexUpdateCheck::WrongFormat;
                break;
            case IndexHeaderState::Incomplete:
                status = IndexUpdateCheck::Incomplete;
                break;
            default:
                status = IndexUpdateCheck::OK;
                break;
            }
        }

        reporter->indexing_status(trace, status);
        doIndexing = (status == IndexUpdateCheck::OK ?
                      Troolean::No : Troolean::Yes);
    } else if (doIndexing == Troolean::Yes) {
        reporter->indexing_status(trace, IndexUpdateCheck::Forced);
    }

    if (doIndexing == Troolean::Yes)
        run_indexer(trace, iparams, idiags, get_parse_params());
}

ParseParams TarmacUtilityBase::get_parse_params() const
{
    ParseParams pparams;
    pparams.bigend = bigend;
    if (thumbonly) {
        pparams.iset_specified = true;
        pparams.iset = THUMB;
    }
    return pparams;
}

void TarmacUtilityBase::setup_noexit()
{
    postProcessOptions();
    if (indexing != Troolean::No)
        setupIndex();
}

void TarmacUtilityBase::setup()
{
    setup_noexit();

    if (onlyIndex)
        exit(0);
}

std::shared_ptr<Image> TarmacUtilityBase::load_image()
{
    std::shared_ptr<Image> image =
        image_filename.empty() ? nullptr
                               : std::make_shared<Image>(image_filename);

    if (image) {
        bool is_big_endian = image->is_big_endian();
        if (bigend_explicit) {
            if (bigend != is_big_endian) {
                reporter->warnx(_("Endianness mismatch between image and "
                                  "provided endianness"));
            }
        } else {
            bigend = is_big_endian;
        }
    }

    return image;
}
