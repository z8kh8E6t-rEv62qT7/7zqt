add_library(z7_ui_common STATIC
  include/archive_suffix_catalog.h
  include/path_history_utils.h
  include/task_background_mode.h
  src/archive_suffix_catalog.cpp
  src/path_history_utils.cpp
  src/task_background_mode.cpp
)

add_library(z7::ui_common ALIAS z7_ui_common)

target_include_directories(z7_ui_common
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_ui_common
  PUBLIC
    Qt6::Core)

z7_set_default_target_options(z7_ui_common)
