add_subdirectory(7zFM)
add_subdirectory(7zG)

set_target_properties(7zFM PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${Z7_EXECUTABLE_OUTPUT_DIR}")
set_target_properties(7zG PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${Z7_EXECUTABLE_OUTPUT_DIR}")

if(TARGET z7_sfx_module_7z)
  add_dependencies(7zFM z7_sfx_module_7z)
  add_dependencies(7zG z7_sfx_module_7z)
endif()
