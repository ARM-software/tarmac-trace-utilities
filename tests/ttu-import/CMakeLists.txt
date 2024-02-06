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

# The purpose of this CMakeLists.txt is to configure, build and install
# (at this CMakeLists.txt configuration time) the current working tree
# of tarmac-trace-utilities, and then build a smaple application using
# the headers and libraries we just installed.

cmake_minimum_required (VERSION 3.5.1)

project(ttu-import LANGUAGES CXX)

option(USE_LIBINTL "Enable i18n" OFF)

# 
set(TTU_CMAKE_ARGS
  -G ${CMAKE_GENERATOR}
  -DCMAKE_CXX_COMPILER:PATH=${CMAKE_CXX_COMPILER}
  -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
  -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}
  -DTTU_SOURCE_DIR:PATH=${CMAKE_CURRENT_LIST_DIR}/../..
  -DUSE_LIBINTL:BOOL=${USE_LIBINTL})
if(DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
  set(TTU_CMAKE_ARGS ${TTU_CMAKE_ARGS} -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=${CMAKE_EXPORT_COMPILE_COMMANDS})
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/export-ttu -B ${CMAKE_BINARY_DIR}/ttu ${TTU_CMAKE_ARGS} 
)
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/ttu
)
execute_process(
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}/ttu
)

set(TarmacTraceUtilities_DIR "${CMAKE_BINARY_DIR}/lib/cmake/TarmacTraceUtilities"
    CACHE PATH "Path to the tarmac-trace-utilities package configuration files")
find_package(TarmacTraceUtilities REQUIRED
  CONFIG
  NO_DEFAULT_PATH
  NO_PACKAGE_ROOT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
)

add_executable(ttu-import ttu-import.cpp)
target_link_libraries(ttu-import TarmacTraceUtilities::tarmac)
