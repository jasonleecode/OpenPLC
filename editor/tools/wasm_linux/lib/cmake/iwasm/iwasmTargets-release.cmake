#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "iwasm::vmlib" for configuration "Release"
set_property(TARGET iwasm::vmlib APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(iwasm::vmlib PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM;C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libiwasm.a"
  )

list(APPEND _cmake_import_check_targets iwasm::vmlib )
list(APPEND _cmake_import_check_files_for_iwasm::vmlib "${_IMPORT_PREFIX}/lib/libiwasm.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
