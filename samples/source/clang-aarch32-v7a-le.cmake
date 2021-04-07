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
# build the example images for little-endian Arm v7-A using clang.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if (NOT clang_binary)
  set(clang_binary clang)
endif()
if (NOT lld_binary)
  set(lld_binary ld.lld)
endif()

set(CMAKE_SYSROOT ${CMAKE_SOURCE_DIR})

set(CMAKE_C_COMPILER ${clang_binary})
set(CMAKE_CXX_COMPILER ${clang_binary})
set(CMAKE_ASM_COMPILER ${clang_binary})

string(APPEND CMAKE_C_FLAGS " --target=arm-arm-none-eabi -march=armv7-a -fno-exceptions -mfloat-abi=hard ")
string(APPEND CMAKE_CXX_FLAGS " --target=arm-arm-none-eabi -march=armv7-a -fno-exceptions -mfloat-abi=hard ")
string(APPEND CMAKE_ASM_FLAGS " --target=arm-arm-none-eabi -march=armv7-a ")

set(CMAKE_C_LINK_EXECUTABLE "${lld_binary} -o <TARGET> <OBJECTS>")
set(CMAKE_CXX_LINK_EXECUTABLE "${lld_binary} -o <TARGET> <OBJECTS>")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
