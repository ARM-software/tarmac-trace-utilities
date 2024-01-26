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
target_link_libraries(tarmac-callinfo tarmac)

add_executable(tarmac-calltree calltree.cpp ${EXTRA_FILES})
target_link_libraries(tarmac-calltree tarmac)

add_executable(tarmac-flamegraph flamegraph.cpp ${EXTRA_FILES})
target_link_libraries(tarmac-flamegraph tarmac)

add_executable(tarmac-profile profileinfo.cpp ${EXTRA_FILES})
target_link_libraries(tarmac-profile tarmac)

add_executable(tarmac-vcd vcdwriter.cpp vcd.cpp ${EXTRA_FILES})
target_link_libraries(tarmac-vcd tarmac)

install_programs(
  tarmac-callinfo tarmac-calltree tarmac-flamegraph tarmac-profile
  tarmac-vcd)

# Test and diagnostic utilities, not installed.

add_executable(tarmac-indextool indextool.cpp)
target_link_libraries(tarmac-indextool tarmac)

add_executable(parsertest parsertest.cpp)
target_link_libraries(parsertest tarmac)

add_executable(exprtest exprtest.cpp)
target_link_libraries(exprtest tarmac)

add_executable(argparsetest argparsetest.cpp)
target_link_libraries(argparsetest tarmac)

add_executable(btodtest btodtest.cpp)
target_link_libraries(btodtest tarmac)

add_executable(avltest avltest.cpp)
target_link_libraries(avltest tarmac)

add_executable(formattest formattest.cpp)
target_link_libraries(formattest tarmac)
