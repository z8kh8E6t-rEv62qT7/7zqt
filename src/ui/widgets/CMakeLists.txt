add_library(z7_ui_widgets STATIC
  include/structured_list_config.h
  include/structured_list_delegate.h
  include/structured_list_proxy.h
  include/structured_list_view.h
  src/structured_list_delegate.cpp
  src/structured_list_proxy.cpp
  src/structured_list_view.cpp
)

add_library(z7::ui_widgets ALIAS z7_ui_widgets)

target_include_directories(z7_ui_widgets
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_ui_widgets
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

set_target_properties(z7_ui_widgets PROPERTIES
  AUTOMOC ON)

z7_set_default_target_options(z7_ui_widgets)
