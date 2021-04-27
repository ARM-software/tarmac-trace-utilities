# Copyright 2016-2021 Arm Limited. All rights reserved.
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

add_executable(tarmac-callinfo main-callinfo.cpp callinfo.cpp)
target_link_libraries(tarmac-callinfo tarmac)

add_executable(tarmac-calltree calltreeviewer.cpp calltree.cpp)
target_link_libraries(tarmac-calltree tarmac)

add_executable(tarmac-flamegraph flamegraph.cpp calltree.cpp)
target_link_libraries(tarmac-flamegraph tarmac)

add_executable(tarmac-profile profileinfo.cpp calltree.cpp)
target_link_libraries(tarmac-profile tarmac)

add_executable(tarmac-vcd vcdwriter.cpp vcd.cpp calltree.cpp)
target_link_libraries(tarmac-vcd tarmac)

set(installed_tools ${installed_tools}
  tarmac-callinfo tarmac-calltree tarmac-flamegraph tarmac-profile
  tarmac-vcd
  PARENT_SCOPE)

# Test and diagnostic utilities, not installed.

add_executable(tarmac-indextool indextool.cpp)
target_link_libraries(tarmac-indextool tarmac)

add_executable(parsertest parsertest.cpp)
target_link_libraries(parsertest tarmac)

add_executable(exprtest exprtest.cpp)
target_link_libraries(exprtest tarmac)