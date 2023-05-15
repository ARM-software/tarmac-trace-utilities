/*
 * Copyright 2016-2023 Arm Limited. All rights reserved.
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

#include "libtarmac/tarmacutil.hh"
#include "libtarmac/index.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/reporter.hh"

#include <iostream>
#include <stdlib.h>

using std::string;

TarmacUtilityBase::TarmacUtilityBase(Argparse &ap, bool can_use_image)
    : verbose(is_interactive()), show_progress_meter(verbose)
{
    if (can_use_image)
        ap.optval({"--image"}, "IMAGEFILE", "image file name",
                  [this](const string &s) { image_filename = s; });
    ap.optnoval({"--only-index"}, "generate index and do nothing else",
                [this]() {
                    indexing = Troolean::Yes;
                    onlyIndex = true;
                });
    ap.optnoval({"--force-index"}, "regenerate index unconditionally",
                [this]() { indexing = Troolean::Yes; });
    ap.optnoval({"--no-index"}, "do not regenerate index",
                [this]() { indexing = Troolean::No; });
    ap.optnoval({"--li"}, "assume trace is from a little-endian platform",
                [this]() {
                    bigend = false;
                    bigend_explicit = true;
                });
    ap.optnoval({"--bi"}, "assume trace is from a big-endian platform",
                [this]() {
                    bigend = true;
                    bigend_explicit = true;
                });
    ap.optnoval({"-v", "--verbose"}, "make tool more verbose",
                [this]() { verbose = true; });
    ap.optnoval({"-q", "--quiet"}, "make tool quiet",
                [this]() { verbose = show_progress_meter = false; });
    ap.optnoval({"--show-progress-meter"},
                "force display of the progress meter",
                [this]() { show_progress_meter = true; });
}

TarmacUtility::TarmacUtility(Argparse &ap, bool can_use_image,
                             bool trace_required)
    : TarmacUtilityBase(ap, can_use_image)
{
    ap.optval({"--index"}, "INDEXFILE", "index file name",
              [this](const string &s) { trace.index_filename = s; });
    ap.positional("TRACEFILE", "Tarmac trace file to read",
                  [this](const string &s) { trace.tarmac_filename = s; },
                  trace_required);
}

static string defaultIndexFilename(string tarmac_filename)
{
    return tarmac_filename + ".index";
}

void TarmacUtility::postProcessOptions()
{
    if (trace.index_filename.empty())
        trace.index_filename = defaultIndexFilename(trace.tarmac_filename);

    std::shared_ptr<Image> image =
        image_filename.empty() ? nullptr
                               : std::make_shared<Image>(image_filename);

    if (image) {
        bool is_big_endian = image->is_big_endian();
        if (bigend_explicit) {
            if (bigend != is_big_endian) {
                reporter->warnx("Endianness mismatch between image and "
                                "provided endianness");
            }
        } else {
            bigend = is_big_endian;
        }
    }
}

TarmacUtilityMT::TarmacUtilityMT(Argparse &ap, bool can_use_image)
    : TarmacUtilityBase(ap, can_use_image)
{
    ap.positional_multiple("TRACEFILE", "Tarmac trace files to read",
                           [this](const string &s) {
                               traces.emplace_back(s, defaultIndexFilename(s));
                           });
}

void TarmacUtilityBase::updateIndexIfNeeded(const TracePair &trace) const
{
    Troolean doIndexing = indexing; // so we can translate Auto into Yes or No

    reporter->set_indexing_verbosity(verbose);
    reporter->set_indexing_progress(show_progress_meter);

    if (doIndexing == Troolean::Auto) {
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
        run_indexer(trace, bigend);
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
