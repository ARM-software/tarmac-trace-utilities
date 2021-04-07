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

# The browsers' sources live in the 'browser' subdirectory for convenience,
# but in the build directory, it's easier to have them at the top level.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(CURSES_FOUND)
  add_executable(tarmac-browser main-browser.cpp browse.cpp curses.cpp)
  target_include_directories(tarmac-browser PRIVATE ${CURSES_INCLUDE_DIRS})
  link_directories(PRIVATE ${CURSES_LIBRARY_DIRS})
  target_link_libraries(tarmac-browser tarmac ${CURSES_LIBRARIES})
  list(APPEND installed_tools tarmac-browser)
else()
  message(WARNING "Not building tarmac-browser (failed to locate curses)")
endif()

if(GTK_FOUND)
  add_executable(tarmac-gui-browser main-browser.cpp browse.cpp gtk.cpp)
  target_include_directories(tarmac-gui-browser PRIVATE ${GTK_INCLUDE_DIRS})
  link_directories(PRIVATE ${GTK_LIBRARY_DIRS})
  target_link_libraries(tarmac-gui-browser tarmac ${GTK_LIBRARIES})
  list(APPEND installed_tools tarmac-gui-browser)
else()
  message(WARNING "Not building tarmac-gui-browser (failed to locate GTK 3)")
endif()

set(installed_tools ${installed_tools} PARENT_SCOPE)
