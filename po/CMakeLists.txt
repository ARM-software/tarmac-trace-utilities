add_subdirectory(en_GB@wide)
add_subdirectory(en_GB@rot13)

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
