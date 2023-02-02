/*
 * Copyright 2023 Arm Limited. All rights reserved.
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

#include <cstdlib>
#include <iostream>
#include <string>

#include "libtarmac/misc.hh"

using std::cout;
using std::endl;
using std::string;

#define TEST(fncall, expected)                                                 \
    do {                                                                       \
        string got = fncall;                                                   \
        if (got == expected) {                                                 \
            pass++;                                                            \
        } else {                                                               \
            cout << "line " << __LINE__ << ": " << #fncall << " returned \""   \
                 << got << "\", expected \"" << expected << "\"" << endl;      \
            fail++;                                                            \
        }                                                                      \
    } while (0)

int main(int argc, char *argv[])
{
    int pass = 0, fail = 0;
    TEST(float_btod(0x7f800001), " NaN");
    TEST(float_btod(0x7f800000), " Inf");
    TEST(float_btod(0x7f7fffff), " 3.40282347e+38");
    TEST(float_btod(0x00800000), " 1.17549435e-38");
    TEST(float_btod(0x807fffff), "-1.17549421e-38");
    TEST(float_btod(0x00000001), " 1.40129846e-45");
    TEST(float_btod(0x00000000), " 0.00000000e+00");
    TEST(float_btod(0x3f804000), " 1.00195312e+00");
    TEST(float_btod(0x3f80c000), " 1.00585938e+00");
    TEST(float_btod(0x3f800000), " 1.00000000e+00");
    TEST(float_btod(0x3f800001), " 1.00000012e+00");
    TEST(float_btod(0x3f7fffff), " 9.99999940e-01");
    TEST(float_btod(0x40490fdb), " 3.14159274e+00");
    TEST(double_btod(0x7ff0000000000001ULL), " NaN");
    TEST(double_btod(0x7ff0000000000000ULL), " Inf");
    TEST(double_btod(0x7fefffffffffffffULL), " 1.7976931348623157e+308");
    TEST(double_btod(0x0010000000000000ULL), " 2.2250738585072014e-308");
    TEST(double_btod(0x800fffffffffffffULL), "-2.2250738585072009e-308");
    TEST(double_btod(0x0000000000000001ULL), " 4.9406564584124654e-324");
    TEST(double_btod(0x0000000000000000ULL), " 0.0000000000000000e+00");
    TEST(double_btod(0x3ff0000800000000ULL), " 1.0000076293945312e+00");
    TEST(double_btod(0x3ff0001800000000ULL), " 1.0000228881835938e+00");
    TEST(double_btod(0x3ff0000000000000ULL), " 1.0000000000000000e+00");
    TEST(double_btod(0x3ff0000000000001ULL), " 1.0000000000000002e+00");
    TEST(double_btod(0x3fefffffffffffffULL), " 9.9999999999999989e-01");
    TEST(double_btod(0x400921fb54442d18ULL), " 3.1415926535897931e+00");
    cout << "pass " << pass << " fail " << fail << endl;
    return fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
