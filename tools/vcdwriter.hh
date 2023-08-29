/*
 * Copyright 2016-2021,2023 Arm Limited. All rights reserved.
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

#ifndef TARMAC_VCDWRITER_HH
#define TARMAC_VCDWRITER_HH

#include "libtarmac/index.hh"
#include "libtarmac/misc.hh"

#include <string>

class VCDWriter : public IndexNavigator {
    using IndexNavigator::IndexNavigator;

  public:
    VCDWriter(const VCDWriter &) = delete;

    void run(const std::string &VCDFilename, bool NoDate,
             bool UseTarmacTimestamp = false);
};

#endif // TARMAC_VCDWRITER_HH
