add_library(z7_ui_archive_support STATIC
  include/archive_quicklook_export.h
  include/archive_delegate_qt.h
  include/archive_format.h
  include/archive_progress_format.h
  include/archive_string_codec_qt.h
  include/archive_virtual_path.h
  src/archive_quicklook_export.cpp
  src/archive_delegate_qt.cpp
  src/archive_format.cpp
  src/archive_progress_format.cpp
  src/archive_string_codec_qt.cpp
  src/archive_virtual_path.cpp
)

add_library(z7::ui_archive_support ALIAS z7_ui_archive_support)

target_include_directories(z7_ui_archive_support
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_ui_archive_support
  PUBLIC
    z7_app_logic
    z7::i18n
    Qt6::Core)

z7_set_default_target_options(z7_ui_archive_support)
