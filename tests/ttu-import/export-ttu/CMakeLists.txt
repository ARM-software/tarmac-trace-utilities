# Copyright 2024 Arm Limited. All rights reserved.
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

# The purpose of this CMakeLists.txt is to configure, build and install the
# tarmac-trace-utilities, as if it was an external dependency, like a
# downstream project would do. Real users of the tarmac-trace-utilities would
# fetch the sources from our github repository whereas for test purposes we
# use the local checkout we already have.

cmake_minimum_required (VERSION 3.5.1)

project(export-ttu LANGUAGES CXX)

option(USE_LIBINTL "Enable i18n" OFF)

set(TTU_CMAKE_ARGS
  -G ${CMAKE_GENERATOR}
  -DCMAKE_CXX_COMPILER:PATH=${CMAKE_CXX_COMPILER}
  -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
  -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
  -DUSE_LIBINTL:BOOL=${USE_LIBINTL})
if(DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
  set(TTU_CMAKE_ARGS ${TTU_CMAKE_ARGS} -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=${CMAKE_EXPORT_COMPILE_COMMANDS})
endif()

include(ExternalProject)

ExternalProject_Add(tarmac-trace-utilities
  PREFIX "external"
  SOURCE_DIR ${TTU_SOURCE_DIR}
  CMAKE_ARGS ${TTU_CMAKE_ARGS}
)