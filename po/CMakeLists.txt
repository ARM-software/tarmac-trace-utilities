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

macro(add_this_po lang)
  gettext_process_po_files(${lang} ALL
    PO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tarmac-trace-utilities.po
    INSTALL_DESTINATION ${CMAKE_INSTALL_LOCALEDIR})

  set(mo_files
    ${mo_files} ${CMAKE_CURRENT_BINARY_DIR}/tarmac-trace-utilities.gmo
    PARENT_SCOPE)
endmacro()

add_subdirectory(en_GB@wide)
add_subdirectory(en_GB@rot13)
add_subdirectory(fr)

if(CMAKE_SYSTEM_NAME MATCHES "Windows" AND Python3_FOUND)
  add_custom_command(OUTPUT translation.rc
    DEPENDS mo2msgres.py ${mo_files}
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mo2msgres.py
      -o translation.rc ${mo_files})

  add_library(translation_res OBJECT translation.rc)
  set(EXTRA_FILES ${EXTRA_FILES} $<TARGET_OBJECTS:translation_res>
    PARENT_SCOPE)
endif()
