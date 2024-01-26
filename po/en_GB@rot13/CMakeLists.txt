gettext_process_po_files(en_GB@rot13 ALL
  PO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tarmac-trace-utilities.po
  INSTALL_DESTINATION ${CMAKE_INSTALL_LOCALEDIR})

set(mo_files
  ${mo_files} ${CMAKE_CURRENT_BINARY_DIR}/tarmac-trace-utilities.gmo
  PARENT_SCOPE)
