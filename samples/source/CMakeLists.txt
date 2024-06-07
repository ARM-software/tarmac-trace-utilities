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

# This CMake project can be used with various toolchains and settings
# to build an assortment of free-standing sample programs. The ELF
# files elsewhere in this repository are derived from these sources.
#
# These images don't need a full toolchain, because they avoid
# depending on any supporting library code at all. They just need a
# compiler and a linker.

cmake_minimum_required(VERSION 3.20)
project(tarmac-trace-utilities-sample-programs)

enable_language(ASM)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

add_executable(hello-world entry.S semihosting.c hello-world.c)
add_executable(quicksort entry.S semihosting.c quicksort.c)
add_executable(calculator entry.S semihosting.c calculator.cpp)
add_executable(fpregs entry.S semihosting.c fpregs.c)

add_executable(calltest1 calltests/calltest1.S)
