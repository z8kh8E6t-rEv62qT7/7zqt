if(NOT APPLE)
  return()
endif()

add_executable(z7_sfx_module_7z
  original_runtime_anchor.cpp
  main.cpp
)

target_link_libraries(z7_sfx_module_7z
  PRIVATE
    z7::app_logic_static_original
    Qt6::Widgets)

target_include_directories(z7_sfx_module_7z
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/archive_application/include"
    "${Z7_ORIGINAL_VENDOR_DIR}"
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP")

set_target_properties(z7_sfx_module_7z
  PROPERTIES
    OUTPUT_NAME "7z.sfx"
    RUNTIME_OUTPUT_DIRECTORY "${Z7_EXECUTABLE_OUTPUT_DIR}")

z7_set_default_target_options(z7_sfx_module_7z)

if(Z7_ENABLE_INSTALL)
  install(TARGETS z7_sfx_module_7z
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
