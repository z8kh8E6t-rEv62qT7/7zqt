add_library(z7_shell_integration_menu STATIC
  include/shell_integration_menu.h
  menu/internal.h
  menu/shell_integration_menu.cpp
  menu/shell_integration_menu_helpers.cpp
  menu/shell_integration_menu_plan.cpp
)

add_library(z7::shell_integration_menu ALIAS z7_shell_integration_menu)

target_include_directories(z7_shell_integration_menu
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_shell_integration_menu
  PUBLIC
    Qt6::Core
    z7::i18n
  PRIVATE
    z7::common)

z7_set_default_target_options(z7_shell_integration_menu)
