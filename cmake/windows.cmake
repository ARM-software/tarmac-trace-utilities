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

# ----------------------------------------------------------------------
# Windows-specific CMake setup.

# To build the console tarmac-browser on Windows, first build
# PDCurses, via a build procedure such as
#
#    git clone https://github.com/wmcbrine/PDCurses
#    cd PDCurses\wincon
#    nmake /f makefile.vc
#
# Then tell this project's CMakeLists.txt where to find the results of
# that build, by means of setting PDCURSES_ROOT to the checkout
# directory, e.g.
#
#    cmake <srcdir> -DPDCURSES_ROOT=c:\Users\whoever\PDCurses
#
# and optionally also defining PDCURSES_BUILD_DIR if you built the
# library in somewhere other than the 'wincon' subdirectory of there.

if(DEFINED PDCURSES_ROOT)
  set(CURSES_FOUND ON)
  set(CURSES_INCLUDE_DIRS ${PDCURSES_ROOT})
  if(NOT PDCURSES_BUILD_DIR)
    set(PDCURSES_BUILD_DIR ${PDCURSES_ROOT}/wincon)
  endif()
  add_library(pdcurses STATIC IMPORTED)
  set_target_properties(pdcurses PROPERTIES
    IMPORTED_LOCATION ${PDCURSES_BUILD_DIR}/pdcurses.lib)
  set(CURSES_LIBRARIES pdcurses)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  # Turn off some too-noisy MSVC compiler warnings:
  #
  # 4244, 4267: possible loss of data when narrowing integers. Often
  # this is done on purpose, and I don't think it makes things clearer
  # to write an explicit static_cast in every case.
  #
  # 4018: signed/unsigned mismatch in comparison. Generally this turns
  # out to be in cases where it obviously can't go wrong anyway, e.g.
  # an 'int' control variable in a for loop whose limit is unsigned
  # but clearly small.
  #
  # 4146: applying unary '-' to an unsigned type. This _could_
  # indicate that you've forgotten the type was unsigned, but it's
  # also a perfectly reasonable thing to do for bit-twiddling purposes.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /wd4267 /wd4018 /wd4146")
endif()

include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
#include <windows.h>
#include <shlobj.h>
int main(void) { (void)FOLDERID_AppDataProgramData; }
" HAVE_APPDATAPROGRAMDATA)

# libintl-based localization within the tools is not supported on
# Windows, but we can still use the gettext command-line utilities to
# build translation files if possible, and then use mo2msgres.py to
# convert them into Windows translation resources.
find_package(Gettext)
set(HAVE_LIBINTL 0)
