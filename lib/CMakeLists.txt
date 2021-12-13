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

# Library files used in common by all the utilities.

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(platform_sources platform/windows.cpp)
else()
  set(platform_sources platform/unix.cpp)
endif()

add_library(tarmac
  argparse.cpp btod.cpp callinfo.cpp calltree.cpp elf.cpp expr.cpp
  image.cpp index.cpp index_ds.cpp misc.cpp parser.cpp registers.cpp
  tarmacutil.cpp ${platform_sources})

set(LIBTARMAC_HEADERS "")
foreach(H argparse.hh callinfo.hh calltree.hh disktree.hh elf.hh expr.hh
    image.hh index.hh index_ds.hh memtree.hh misc.hh parser.hh registers.hh
    reporter.hh tarmacutil.hh)
    list(APPEND LIBTARMAC_HEADERS ${CMAKE_SOURCE_DIR}/include/libtarmac/${H})
endforeach()
set_target_properties(tarmac PROPERTIES PUBLIC_HEADER "${LIBTARMAC_HEADERS}")

install(TARGETS tarmac
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include/libtarmac)
