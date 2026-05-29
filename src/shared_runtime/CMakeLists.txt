set(Z7_SHARED_RUNTIME_WHOLE_ARCHIVE_TARGETS
  z7_app_logic
  z7_task_ipc_runtime
  z7_platform_qt
  z7_ui_runtime_support
  z7_ui_gui
  z7_ui_filemanager)

set(Z7_SHARED_RUNTIME_PIC_TARGETS
  z7_app_logic
  z7_task_ipc_runtime
  z7_shell_integration_menu
  z7_platform_qt
  z7_ui_common
  z7_ui_archive_support
  z7_ui_runtime_support
  z7_ui_widgets
  z7_ui_gui
  z7_ui_filemanager)

if(APPLE AND TARGET z7_macos_integration_launch_services)
  list(APPEND Z7_SHARED_RUNTIME_PIC_TARGETS
    z7_macos_integration_launch_services)
endif()

if(APPLE AND TARGET z7_macos_integration_core)
  list(APPEND Z7_SHARED_RUNTIME_WHOLE_ARCHIVE_TARGETS
    z7_macos_integration_core
    z7_macos_integration_capi
    z7_macos_integration_native_drag)
  list(APPEND Z7_SHARED_RUNTIME_PIC_TARGETS
    z7_macos_integration_core
    z7_macos_integration_capi
    z7_macos_integration_native_drag)
endif()

foreach(z7_shared_runtime_pic_target IN LISTS Z7_SHARED_RUNTIME_PIC_TARGETS)
  if(TARGET "${z7_shared_runtime_pic_target}")
    set_property(TARGET "${z7_shared_runtime_pic_target}" PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
endforeach()

set(Z7_SHARED_RUNTIME_LINK_ITEMS)
foreach(z7_shared_runtime_target IN LISTS Z7_SHARED_RUNTIME_WHOLE_ARCHIVE_TARGETS)
  if(TARGET "${z7_shared_runtime_target}")
    list(APPEND Z7_SHARED_RUNTIME_LINK_ITEMS
      "$<LINK_LIBRARY:WHOLE_ARCHIVE,${z7_shared_runtime_target}>")
  endif()
endforeach()

add_library(z7_shared_runtime SHARED
  shared_runtime_stub.cpp)

add_library(z7::shared_runtime ALIAS z7_shared_runtime)

target_link_libraries(z7_shared_runtime
  PRIVATE
    ${Z7_SHARED_RUNTIME_LINK_ITEMS}
    z7_third_party)

if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_shared_runtime
    PUBLIC
      Z7_TESTING)
endif()

set_target_properties(z7_shared_runtime
  PROPERTIES
    OUTPUT_NAME z7_shared_runtime
    LIBRARY_OUTPUT_DIRECTORY "${Z7_SHARED_LIBRARY_OUTPUT_DIR}")

if(WIN32)
  set_target_properties(z7_shared_runtime
    PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${Z7_SHARED_LIBRARY_OUTPUT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY "${Z7_EXECUTABLE_OUTPUT_DIR}"
      WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
  if(APPLE)
    set_target_properties(z7_shared_runtime
      PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path")
  else()
    set_target_properties(z7_shared_runtime
      PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
  endif()
endif()

z7_set_default_target_options(z7_shared_runtime)

if(Z7_ENABLE_INSTALL AND NOT APPLE)
  install(TARGETS z7_shared_runtime
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
