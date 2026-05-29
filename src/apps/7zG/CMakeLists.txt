add_executable(7zG
  cli_bridge.cpp
  cli_bridge.h
  main.cpp
)

if(WIN32)
  set(Z7_G_ICON_FILE
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP/7zip/UI/GUI/7zG.ico")
  if(EXISTS "${Z7_G_ICON_FILE}")
    set(Z7_G_ICON_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(GENERATE
      OUTPUT "${Z7_G_ICON_RC_FILE}"
      CONTENT "IDI_ICON ICON \"${Z7_G_ICON_FILE}\"\n")
    target_sources(7zG PRIVATE "${Z7_G_ICON_RC_FILE}")
  endif()
endif()

target_link_libraries(7zG
  PRIVATE
    z7_shared_runtime
    z7::third_party
    Qt6::Widgets)

target_include_directories(7zG
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/qt_application_support/include"
    "${PROJECT_SOURCE_DIR}/src/task_ipc_runtime/include"
    "${PROJECT_SOURCE_DIR}/src/ui/gui/include"
    "${PROJECT_SOURCE_DIR}/src/ui/runtime_support/include"
    "${Z7_ORIGINAL_VENDOR_DIR}"
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP")

if(APPLE)
  set_target_properties(7zG
    PROPERTIES
      BUILD_RPATH "@loader_path/../lib;@executable_path/../Frameworks")
else()
  set_target_properties(7zG
    PROPERTIES
      BUILD_RPATH "$ORIGIN/../lib")
endif()

z7_set_default_target_options(7zG)

if(Z7_ENABLE_INSTALL)
  install(TARGETS 7zG
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
