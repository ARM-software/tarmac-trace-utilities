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

#ifndef TARMAC_CALLINFO_HH
#define TARMAC_CALLINFO_HH

#include "libtarmac/index.hh"
#include "libtarmac/misc.hh"

#include <string>
#include <vector>

class CallInfo : public IndexNavigator {
    using IndexNavigator::IndexNavigator;

  public:
    void run(const std::vector<std::string> &functions);

  private:
    void run(Addr symb_addr);
    void run(const std::string &symb_name);
};

#endif // TARMAC_CALLINFO_HH
