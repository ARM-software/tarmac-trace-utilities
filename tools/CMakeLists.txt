# Copyright 2016-2021,2023 Arm Limited. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# This file is part of Tarmac Trace Utilities

# The tools' sources live in the 'tools' subdirectory for convenience,
# but in the build directory, it's easier to have them at the top level.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(tarmac-callinfo callinfo.cpp ${EXTRA_FILES})
standard_target_configuration(tarmac-callinfo)

add_executable(tarmac-calltree calltree.cpp ${EXTRA_FILES})
standard_target_configuration(tarmac-calltree)

add_executable(tarmac-flamegraph flamegraph.cpp ${EXTRA_FILES})
standard_target_configuration(tarmac-flamegraph)

add_executable(tarmac-profile profileinfo.cpp ${EXTRA_FILES})
standard_target_configuration(tarmac-profile)

add_executable(tarmac-vcd vcdwriter.cpp vcd.cpp ${EXTRA_FILES})
standard_target_configuration(tarmac-vcd)

install_programs(
  tarmac-callinfo tarmac-calltree tarmac-flamegraph tarmac-profile
  tarmac-vcd)

# Test and diagnostic utilities, not installed.

add_executable(tarmac-indextool indextool.cpp)
standard_target_configuration(tarmac-indextool)

add_executable(parsertest parsertest.cpp)
standard_target_configuration(parsertest)

add_executable(exprtest exprtest.cpp)
standard_target_configuration(exprtest)

add_executable(argparsetest argparsetest.cpp)
standard_target_configuration(argparsetest)

add_executable(btodtest btodtest.cpp)
standard_target_configuration(btodtest)

add_executable(avltest avltest.cpp)
standard_target_configuration(avltest)

add_executable(formattest formattest.cpp)
standard_target_configuration(formattest)
