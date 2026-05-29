set(Z7_ORIGINAL_LANG_DIR "${Z7_ORIGINAL_VENDOR_DIR}/Lang")
set(Z7_ORIGINAL_FM_DIR "${Z7_ORIGINAL_VENDOR_DIR}/CPP/7zip/UI/FileManager")
set(Z7_ORIGINAL_ARCHIVE_ICON_DIR
  "${Z7_ORIGINAL_VENDOR_DIR}/CPP/7zip/Archive/Icons")

file(GLOB Z7_OFFICIAL_LANG_FILES
  CONFIGURE_DEPENDS
    "${Z7_ORIGINAL_LANG_DIR}/*.txt"
    "${Z7_ORIGINAL_LANG_DIR}/en.ttt")

set(Z7_FM_ICON_FILES
  "${Z7_ORIGINAL_FM_DIR}/Add.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Extract.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Test.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Copy.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Move.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Delete.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Info.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Add2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Extract2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Test2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Copy2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Move2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Delete2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/Info2.bmp"
  "${Z7_ORIGINAL_FM_DIR}/FM.ico")

file(GLOB Z7_ARCHIVE_ICON_FILES
  CONFIGURE_DEPENDS
    "${Z7_ORIGINAL_ARCHIVE_ICON_DIR}/*.ico")

set(Z7_GENERATED_RESOURCE_ROOT
  "${CMAKE_CURRENT_BINARY_DIR}/generated_filemanager_resources")
set(Z7_GENERATED_RESOURCE_QRC
  "${CMAKE_CURRENT_BINARY_DIR}/generated_filemanager_resources.qrc")

add_custom_command(
  OUTPUT
    "${Z7_GENERATED_RESOURCE_QRC}"
  COMMAND
    ${CMAKE_COMMAND}
      -DSOURCE_LANG_DIR=${Z7_ORIGINAL_LANG_DIR}
      -DSOURCE_FM_DIR=${Z7_ORIGINAL_FM_DIR}
      -DSOURCE_ARCHIVE_ICON_DIR=${Z7_ORIGINAL_ARCHIVE_ICON_DIR}
      -DOUT_DIR=${Z7_GENERATED_RESOURCE_ROOT}
      -DOUT_QRC=${Z7_GENERATED_RESOURCE_QRC}
      -P "${PROJECT_SOURCE_DIR}/cmake/GenerateFileManagerResources.cmake"
  DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/GenerateFileManagerResources.cmake"
    ${Z7_OFFICIAL_LANG_FILES}
    ${Z7_FM_ICON_FILES}
    ${Z7_ARCHIVE_ICON_FILES}
  VERBATIM)

add_library(z7_ui_runtime_support STATIC
  include/extract_memory_settings.h
  include/custom_localization.h
  include/large_pages_settings.h
  include/platform_support.h
  include/official_lang_catalog.h
  include/password_prompt_dialog.h
  include/task_progress_dialog_base.h
  src/extract_memory_settings.cpp
  src/json_localization.cpp
  src/large_pages_settings.cpp
  src/official_lang_catalog_internal.cpp
  src/official_lang_catalog.cpp
  src/password_prompt_dialog.cpp
  src/platform_support.cpp
  src/task_progress_dialog_base.cpp
  src/task_progress_dialog_base_cancel.cpp
  src/task_progress_dialog_base_progress.cpp
  src/task_progress_dialog_base_state.cpp
  "${Z7_GENERATED_RESOURCE_QRC}"
)

add_library(z7::ui_runtime_support ALIAS z7_ui_runtime_support)

target_include_directories(z7_ui_runtime_support
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/i18n/src")

target_link_libraries(z7_ui_runtime_support
  PUBLIC
    z7::i18n
    z7_platform_qt
    z7_ui_archive_support
    Qt6::Core
    Qt6::Widgets
  PRIVATE
    z7_app_logic)

z7_set_default_target_options(z7_ui_runtime_support)
