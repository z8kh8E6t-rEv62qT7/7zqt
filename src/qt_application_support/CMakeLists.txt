add_library(z7_platform_qt STATIC
  include/app_startup_qt.h
  include/drag_drop_policy_qt.h
  include/filemanager_instance_launcher.h
  include/portable_settings.h
  include/settings_store_qt.h
  src/settings_store_qt.cpp
  src/portable_settings.cpp
  src/portable_settings_internal.cpp
  src/app_startup_qt.cpp
  src/drag_drop_policy_qt.cpp
  src/filemanager_instance_launcher.cpp
)

add_library(z7::platform_qt ALIAS z7_platform_qt)

target_include_directories(z7_platform_qt
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_platform_qt
  PUBLIC
    z7_app_logic
    z7::i18n
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

if(APPLE AND TARGET z7_macos_integration_launch_services)
  target_link_libraries(z7_platform_qt PRIVATE z7_macos_integration_launch_services)
endif()

z7_set_default_target_options(z7_platform_qt)

if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_platform_qt
    PUBLIC
      Z7_TESTING)
endif()
