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

# Toolchain file for use with this directory's CMakeLists.txt, to
# build the example images for little-endian Arm v8.1-M and MVE using
# Arm Compiler.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_ARCH armv8.1-m.main+fp.dp+mve.fp)

# CMake's Arm Compiler toolchain support will complain if
# CMAKE_SYSTEM_ARCH isn't in its list of known architectures. This one
# isn't, because it has extra "+extension" suffixes. We work around
# that here by resetting the lists of known architectures (one per
# language) so that they do contain our setting!
set(CMAKE_C_COMPILER_ARCH_LIST ${CMAKE_SYSTEM_ARCH})
set(CMAKE_CXX_COMPILER_ARCH_LIST ${CMAKE_SYSTEM_ARCH})
set(CMAKE_ASM_COMPILER_ARCH_LIST ${CMAKE_SYSTEM_ARCH})

set(CMAKE_C_COMPILER armclang)
set(CMAKE_CXX_COMPILER armclang)
set(CMAKE_ASM_COMPILER armclang)

string(APPEND CMAKE_C_FLAGS " --target=arm-arm-none-eabi -march=${CMAKE_SYSTEM_ARCH} -mlittle-endian -fno-exceptions -mfloat-abi=hard ")
string(APPEND CMAKE_CXX_FLAGS " --target=arm-arm-none-eabi -march=${CMAKE_SYSTEM_ARCH} -mlittle-endian -fno-exceptions -mfloat-abi=hard ")
string(APPEND CMAKE_ASM_FLAGS " --target=arm-arm-none-eabi -march=${CMAKE_SYSTEM_ARCH} -mlittle-endian -mthumb ")
string(APPEND CMAKE_C_LINK_FLAGS " --entry=_start ")
string(APPEND CMAKE_CXX_LINK_FLAGS " --entry=_start ")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
