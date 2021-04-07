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

cmake_minimum_required (VERSION 3.5.1)

set(CMAKE_CXX_STANDARD 14)

project(tarmac-trace-utilities
  LANGUAGES CXX)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  include(cmake/windows.cmake)
else()
  include(cmake/unix.cmake)
endif()

# Set up the header file directory, and build the library of common
# code.

include_directories(PRIVATE include)
add_subdirectory(lib)

# Build the executable programs.

set(installed_tools) # subdirectories will update this
add_subdirectory(tools)
add_subdirectory(browser)

# Set the end-user tools to be installed by 'make install' or similar.

if(CMAKE_VERSION VERSION_LESS "3.14")
  # For cmake pre-3.14, we have to specify a destination directory.
  install(TARGETS ${installed_tools} RUNTIME DESTINATION bin)
else()
  # For 3.14 and better, we can let the destination be specified
  # automatically, which makes it reconfigurable independently of the
  # install prefix, by setting CMAKE_INSTALL_BINDIR.
  install(TARGETS ${installed_tools})
endif()

# Check if we have Python3, which is needed for the test suite.

if(CMAKE_VERSION VERSION_LESS "3.12")
  # The method of finding Python was different before CMake 3.12.
  set(Python_ADDITIONAL_VERSIONS 3)
  find_package (PythonInterp)
  if(PYTHONINTERP_FOUND AND NOT PYTHON_VERSION_MAJOR LESS 3)
    set(can_run_tests ON)
    set(python_exe ${PYTHON_EXECUTABLE})
  endif()
else()
  # Up-to-date approach.
  find_package (Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(can_run_tests ON)
    set(python_exe ${Python3_EXECUTABLE})
  endif()
endif()

# Run the test suite.

if(can_run_tests)
  enable_testing()
  add_subdirectory(tests)
else()
  message(WARNING "Not running tests (failed to locate Python 3)")
endif()
