# Copyright 2016-2021,2024 Arm Limited. All rights reserved.
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

set(TTU_package_name "TarmacTraceUtilities")
set(TTU_targets_export_name "${TTU_package_name}Targets")
set(TTU_version 0.0.0)

project(tarmac-trace-utilities
  LANGUAGES CXX)

# Check if we have Python3, which is needed for the test suite.

if(CMAKE_VERSION VERSION_LESS "3.12")
  # The method of finding Python was different before CMake 3.12.
  set(Python_ADDITIONAL_VERSIONS 3)
  find_package (PythonInterp ${REQUIRED_PACKAGE})
  if(PYTHONINTERP_FOUND AND NOT PYTHON_VERSION_MAJOR LESS 3)
    set(can_run_tests ON)
    set(python_exe ${PYTHON_EXECUTABLE})
  endif()
else()
  # Up-to-date approach.
  find_package (Python3 ${REQUIRED_PACKAGE} COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(can_run_tests ON)
    set(python_exe ${Python3_EXECUTABLE})
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  include(cmake/windows.cmake)
else()
  include(cmake/unix.cmake)
endif()

# Function to add standard include/library configuration that applies
# to all Tarmac tools. This includes the 'libtarmac' library built
# from our own lib subdirectory, and also any common dependencies.
function(standard_target_configuration target)
  target_link_libraries(${target} tarmac)
  if(HAVE_LIBINTL)
    # If libtarmac expects to call libintl, we'll need libintl's
    # include and library files.
    target_include_directories(${target} PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(${target} ${Intl_LIBRARIES})
  endif()
endfunction()

# Provide developpers with a strict mode to ensure that all parts of
# tarmac-trace-utilities are built and tested.
set(FORCE_BUILDING_ALL_APPS OFF CACHE BOOL "Fail if not all applications can be built because of missing dependencies")

if(FORCE_BUILDING_ALL_APPS)
  set(REQUIRED_PACKAGE "REQUIRED")
else()
  set(REQUIRED_PACKAGE "")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows"
    AND NOT CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  # When cross-compiling for Windows from Unix, find_package seems to
  # do the wrong thing unless you pre-set some variables strangely. I
  # think it may be thinking more in terms of cross-compiling in the
  # other direction.
  set(WIN32 ON)
  set(CMAKE_CROSSCOMPILING OFF)
  find_package(wxWidgets ${REQUIRED_PACKAGE} COMPONENTS core base)
  set(CMAKE_CROSSCOMPILING ON)
else()
  find_package(wxWidgets ${REQUIRED_PACKAGE} COMPONENTS core base)
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/libtarmac)
configure_file(cmake/cmake.h.in ${CMAKE_BINARY_DIR}/include/libtarmac/cmake.h
  ESCAPE_QUOTES)

# Support large Tarmac traces if possible on this system.
# Note: OFF_T is used in the code base to represent both a file offset
# AND a pointer offset (the index files are memory mapped at some point).
include(CheckTypeSize)
check_type_size(off_t OFF_T LANGUAGE CXX)
if (HAVE_OFF_T)
  if(CMAKE_CXX_SIZEOF_DATA_PTR EQUAL 8)
    if(OFF_T EQUAL 8)
      # Nothing to do, off_t is 64-bit by default on this 64-bit pointers system.
      set(OFF_TY off_t)
    else()
      # Force OFF_T to be 64-bit for systems where we know how to do it, and
      # fall-back to whatever is the default for other systems.
      if(CMAKE_SYSTEM_NAME MATCHES "Windows")
        set(OFF_TY int64_t)
      elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(OFF_TY off_t)
        set(_FILE_OFFSET_BITS 64)
      else()
        message(WARNING "Large Tarmac files are not supported: don't know how to get a 64-bit file offset on this system.")
        set(OFF_TY off_t)
      endif()
    endif()
  else()
    # The system has 32-bit pointers, there is not much we can do.
    set(OFF_TY off_t)
    message(WARNING "Large Tarmac files are not supported on this platform.")
  endif()
else()
  message(FATAL_ERROR "off_t is not available on this platform")
endif()
configure_file(include/libtarmac/platform.hh.in ${CMAKE_BINARY_DIR}/include/libtarmac/platform.hh)

# Build the library of common code.

add_subdirectory(lib)

# Macro to mark a binary as to be installed by 'make install' or
# similar. Implementation has to vary depending on cmake version.

if(CMAKE_VERSION VERSION_LESS "3.14")
  # For cmake pre-3.14, we have to specify a destination directory.
  macro(install_programs)
    install(TARGETS ${ARGN} RUNTIME DESTINATION bin)
  endmacro()
else()
  # For 3.14 and better, we can let the destination be specified
  # automatically, which makes it reconfigurable independently of the
  # install prefix, by setting CMAKE_INSTALL_BINDIR.
  macro(install_programs)
    install(TARGETS ${ARGN})
  endmacro()
endif()

# Translate the messages, if possible.

if(GETTEXT_FOUND)
  add_subdirectory(po)
endif()

# Build the executable programs.

add_subdirectory(tools)
add_subdirectory(browser)

# Run the test suite.

if(can_run_tests)
  enable_testing()
  add_subdirectory(tests)
else()
  message(WARNING "Not running tests (failed to locate Python 3)")
endif()

# Provide downstream users a package configuration information so they can
# include, build and use our tarmac-trace-utilities with minimal fuss --- if
# they work with CMake. The package information can be used with CMake's
# find_package command.
set(TTU_config_file "${TTU_package_name}Config.cmake"
    CACHE STRING "Name of the CMake package config file")
set(TTU_version_file "${TTU_package_name}ConfigVersion.cmake"
    CACHE STRING "Name of the CMake package version file")
set(TTU_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/lib/cmake/${TTU_package_name}"
    CACHE PATH "Directory where the CMake package information will be generated")
set(TTU_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${TTU_package_name}"
    CACHE PATH "Directory where the CMake package information will be installed")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${TTU_generated_dir}/${TTU_version_file}
  VERSION ${TTU_version}
  COMPATIBILITY AnyNewerVersion)
install(EXPORT
    ${TTU_targets_export_name}
  COMPONENT "${PROJECT_NAME}"
  NAMESPACE "${TTU_package_name}::"
  DESTINATION ${TTU_install_dir}
  EXPORT_LINK_INTERFACE_LIBRARIES)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${TTU_config_file}.in
  ${TTU_generated_dir}/${TTU_config_file}
  INSTALL_DESTINATION ${TTU_install_dir})
install(
  FILES
    ${TTU_generated_dir}/${TTU_config_file}
    ${TTU_generated_dir}/${TTU_version_file}
  COMPONENT "${PROJECT_NAME}"
  DESTINATION ${TTU_install_dir})