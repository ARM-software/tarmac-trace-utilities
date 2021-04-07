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

#include <chrono>
#include <iostream>
#include <string>

/*
 * Handy class to allow timing how long something takes. You simply
 * create an instance of this class, and when it goes out of scope,
 * the destructor prints the elapsed time since the object was
 * constructed.
 */
class StopWatch {
    using Clock = std::chrono::high_resolution_clock;
    using Rep = typename Clock::duration::rep;
    using Units = typename Clock::duration;

    const typename Clock::time_point Start;
    const std::string Name;

  public:
    StopWatch(const std::string &Name) : Start(Clock::now()), Name(Name) {}

    ~StopWatch()
    {
        auto current_time =
            std::chrono::duration_cast<Units>(Clock::now() - Start).count();
        std::cerr << "StopWatch(" << Name
                  << ") : " << static_cast<Rep>(current_time) << std::endl;
    }
};
