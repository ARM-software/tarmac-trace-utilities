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

#include "profileinfo.hh"
#include "libtarmac/calltree.hh"
#include "libtarmac/image.hh"
#include "libtarmac/intl.hh"
#include "libtarmac/reporter.hh"

#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

using namespace std;

struct ProfileData {
    unsigned long Count;
    unsigned long CumulatedCycleCount; // Including all callees

    ProfileData(unsigned long Count, unsigned long CycleCount)
        : Count(Count), CumulatedCycleCount(CycleCount)
    {
    }
    ProfileData() = default;
    ProfileData(const ProfileData &) = default;
    ProfileData &operator=(const ProfileData &) = default;

    void addCall(unsigned long CycleCount)
    {
        Count++;
        CumulatedCycleCount += CycleCount;
    }
};

class Profiler : public CallTreeVisitor {
    using CallTreeVisitor::CallTreeVisitor;

    map<Addr, ProfileData> Prof;

  public:
    void onFunctionEntry(const TarmacSite &function_entry,
                         const TarmacSite &function_exit)
    {
        map<Addr, ProfileData>::iterator it = Prof.find(function_entry.addr);
        if (it == Prof.end())
            Prof.insert(make_pair(
                function_entry.addr,
                ProfileData(1, function_exit.time - function_entry.time + 1)));
        else
            it->second.addCall(function_exit.time - function_entry.time + 1);
    }

    void dump() const
    {
        cout << left << setw(12) << _("Address");
        cout << left << setw(12) << _("Count");
        cout << left << setw(12) << _("Time");
        cout << left << _("Function name");
        cout << '\n';

        for (const auto &p : Prof) {
            ostringstream addr;
            addr << "0x" << hex << p.first;

            cout << left << setw(11) << addr.str() << ' ';
            cout << left << setw(11) << p.second.Count << ' ';
            cout << left << setw(11) << p.second.CumulatedCycleCount << ' ';
            cout << left << CT.getFunctionName(p.first);
            cout << '\n';
        }
    }
};

void ProfileInfo::run()
{
    CallTree CT(*this);
    Profiler P(CT);
    CT.visit(P);

    P.dump();
}

#include "libtarmac/argparse.hh"
#include "libtarmac/tarmacutil.hh"

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    gettext_setup(true);

    IndexerParams iparams;
    iparams.record_memory = false;

    Argparse ap("tarmac-profile", argc, argv);
    TarmacUtility tu;
    tu.set_indexer_params(iparams);
    tu.add_options(ap);
    ap.parse();
    tu.setup();

    ProfileInfo PI(tu.trace, tu.image_filename, tu.load_offset);
    PI.run();

    return 0;
}
