add_library(z7_7zfm_app_support STATIC
  file_open_support.cpp
  file_open_support.h
)

target_link_libraries(z7_7zfm_app_support
  PUBLIC
    Qt6::Widgets)

target_include_directories(z7_7zfm_app_support
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

z7_set_default_target_options(z7_7zfm_app_support)

add_executable(7zFM
  main.cpp
)

if(WIN32)
  set(Z7_FM_ICON_FILE
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP/7zip/UI/FileManager/FM.ico")
  set(Z7_FM_ICON_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
  file(GENERATE
    OUTPUT "${Z7_FM_ICON_RC_FILE}"
    CONTENT "IDI_ICON ICON \"${Z7_FM_ICON_FILE}\"\n")
  target_sources(7zFM PRIVATE "${Z7_FM_ICON_RC_FILE}")
endif()

if(APPLE)
  set_target_properties(7zFM PROPERTIES
    OUTPUT_NAME "7zFM"
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "7zFM"
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.sevenzip"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_LONG_VERSION_STRING "7-Zip File Manager 1.0")
endif()

target_link_libraries(7zFM
  PRIVATE
    z7_7zfm_app_support
    z7_shared_runtime
    Qt6::Widgets)

target_include_directories(7zFM
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/archive_application/include"
    "${PROJECT_SOURCE_DIR}/src/qt_application_support/include"
    "${PROJECT_SOURCE_DIR}/src/ui/filemanager/include"
    "${PROJECT_SOURCE_DIR}/src/ui/runtime_support/include")

if(APPLE)
  set_target_properties(7zFM
    PROPERTIES
      BUILD_RPATH "@loader_path/../lib;@executable_path/../Frameworks")
else()
  set_target_properties(7zFM
    PROPERTIES
      BUILD_RPATH "$ORIGIN/../lib")
endif()

z7_set_default_target_options(7zFM)

if(Z7_ENABLE_INSTALL)
  if(APPLE)
    install(TARGETS 7zFM
      BUNDLE DESTINATION ".")
  else()
    install(TARGETS 7zFM
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  endif()
endif()
