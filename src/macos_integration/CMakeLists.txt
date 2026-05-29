add_library(z7_macos_integration_core STATIC
  include/macos_integration_config.h
  config/macos_integration_config.cpp
)

add_library(z7::macos_integration_core ALIAS z7_macos_integration_core)

add_library(z7_macos_integration_capi STATIC
  include/macos_integration_c_api.h
  capi/internal.h
  capi/macos_integration_c_api_menu_helpers.h
  capi/password_relay_delegate.h
  capi/macos_integration_c_api_archive_runtime.cpp
  capi/macos_integration_c_api_common.cpp
  capi/macos_integration_c_api_error_fallback.cpp
  capi/macos_integration_c_api_environment.cpp
  capi/macos_integration_c_api_menu_action.cpp
  capi/macos_integration_c_api_menu_helpers.cpp
  capi/macos_integration_c_api_menu_plan.cpp
  capi/macos_integration_c_api_nested_session.cpp
  capi/macos_integration_c_api_password.cpp
  capi/password_relay_delegate.cpp
  capi/macos_integration_c_api_session.cpp
  capi/macos_integration_c_api_task.cpp
  capi/macos_integration_c_api_task_state.cpp
  capi/macos_integration_c_api_quicklook_list.cpp
  capi/macos_integration_c_api_quicklook_batch_export.cpp
)

add_library(z7::macos_integration_capi ALIAS z7_macos_integration_capi)

add_library(z7_macos_integration_native_drag STATIC
  include/macos_native_drag.h
  native_drag/src/internal.h
  native_drag/src/drag_drop_native_macos.cpp
  native_drag/src/drag_drop_native_macos_concurrency.cpp
  native_drag/src/drag_drop_native_macos_policy.cpp
  native_drag/src/drag_drop_native_macos_materialize.cpp
  native_drag/src/drag_drop_native_macos_state.cpp
)

if(APPLE)
  enable_language(OBJCXX)
  find_library(Z7_UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK
    UniformTypeIdentifiers
    REQUIRED)
  set_source_files_properties(
    native_drag/src/drag_drop_native_macos.cpp
    native_drag/src/drag_drop_native_macos_concurrency.cpp
    native_drag/src/drag_drop_native_macos_policy.cpp
    native_drag/src/drag_drop_native_macos_materialize.cpp
    native_drag/src/drag_drop_native_macos_state.cpp
    PROPERTIES
      LANGUAGE OBJCXX
  )
endif()

add_library(z7::macos_integration_native_drag ALIAS z7_macos_integration_native_drag)

target_include_directories(z7_macos_integration_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_include_directories(z7_macos_integration_capi
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_include_directories(z7_macos_integration_native_drag
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_macos_integration_native_drag
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

if(APPLE)
  target_link_libraries(z7_macos_integration_native_drag
    PRIVATE
      "${Z7_UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK}")
endif()

target_link_libraries(z7_macos_integration_core
  PUBLIC
    Qt6::Core
    z7_shell_integration_menu
    z7_platform_qt
    z7_ui_runtime_support)

target_link_libraries(z7_macos_integration_capi
  PUBLIC
    Qt6::Core
    z7_macos_integration_core
  PRIVATE
    z7_app_logic
    z7::common
    z7::i18n
    z7_ui_common
    z7_ui_archive_support
    z7_platform_qt
    z7_task_ipc_runtime)

z7_set_default_target_options(z7_macos_integration_capi)
z7_set_default_target_options(z7_macos_integration_native_drag)
z7_set_default_target_options(z7_macos_integration_core)
