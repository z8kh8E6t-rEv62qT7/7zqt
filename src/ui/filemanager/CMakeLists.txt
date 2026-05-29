set(Z7_UI_FILEMANAGER_RUNNER_SOURCES
  include/archive_process_runner.h
  src/archive_process_runner/helpers.h
  src/archive_process_runner/core_prompts.h
  src/archive_process_runner/core.cpp
  src/archive_process_runner/core_prompts.cpp
  src/archive_process_runner/start_archive.cpp
  src/archive_process_runner/start_file.cpp
  src/archive_process_runner/helpers.cpp
)

set(Z7_UI_FILEMANAGER_MAIN_WINDOW_SOURCES
  include/main_window.h
  include/display_settings.h
  include/shared/column_width_persistence.h
  include/shared/external_command_parser.h
  src/support/column_width_persistence.cpp
  src/settings/display_settings.cpp
  src/main_window/internal.h
  src/main_window/deps.h
  src/main_window/actions/action_favorites.cpp
  src/main_window/actions/action_file_ops.cpp
  src/main_window/actions/action_properties.cpp
  src/main_window/actions/action_select.cpp
  src/main_window/actions/action_shell.cpp
  src/main_window/actions/action_tasks.cpp
  src/main_window/actions/task.cpp
  src/main_window/archive/archive_open_actions_tracking_platform.cpp
  src/main_window/archive/archive_open_actions.cpp
  src/main_window/archive/archive_open_actions_inside.cpp
  src/main_window/archive/archive_open_actions_outside.cpp
  src/main_window/archive/archive_virtual_view.cpp
  src/main_window/archive/archive_writeback.cpp
  src/main_window/core/main_window_constructor.cpp
  src/main_window/core/core_open.cpp
  src/main_window/core/core_path.cpp
  src/main_window/core/core_view.cpp
  src/main_window/dialogs/archive_add_sources_dialog.h
  src/main_window/dialogs/archive_add_sources_dialog.cpp
  src/main_window/drag_drop/core_path_drop.cpp
  src/main_window/drag_drop/core_path_drop_handlers.cpp
  src/main_window/drag_drop/core_path_drop_handlers_drop.cpp
  src/main_window/drag_drop/drag_aware_views.h
  src/main_window/drag_drop/drag_aware_views.cpp
  src/main_window/drag_drop/drag_source_marker.h
  src/main_window/drag_drop/drag_source_marker.cpp
  src/main_window/drag_drop/drop_effect_feedback.h
  src/main_window/drag_drop/drop_effect_feedback.cpp
  src/main_window/drag_drop/drop_logic.h
  src/main_window/drag_drop/drop_logic.cpp
  src/main_window/drag_drop/drop_logic_decision.cpp
  src/main_window/model/model.h
  src/main_window/model/directory_list_model_data.cpp
  src/main_window/model/directory_list_model_render.cpp
  src/main_window/model/directory_list_model_state.cpp
  src/main_window/navigation/nav_menu.cpp
  src/main_window/navigation/nav_panel.cpp
  src/main_window/navigation/nav_task_ipc.cpp
  src/main_window/navigation/nav_task_ipc_compress_hash.cpp
  src/main_window/open/open_commands.cpp
  src/main_window/open/open_commands_temp_session.cpp
  src/main_window/open/open_tasks.cpp
  src/main_window/state/state_format.cpp
  src/main_window/state/state_names_utils.cpp
  src/main_window/state/state_panel_archive.cpp
  src/main_window/state/state_panel_runtime.cpp
  src/main_window/state/state_panel_selection.cpp
  src/main_window/state/state_panel_view.cpp
  src/main_window/state/state_window.cpp
  src/main_window/view/view_actions_setup.cpp
  src/main_window/view/view_actions_connections.cpp
  src/main_window/view/view_runtime.cpp
  src/main_window/view/view_state.cpp
  src/main_window/view/view_widgets.cpp
)

set(Z7_UI_FILEMANAGER_OPTIONS_SOURCES
  include/options_dialog.h
  include/shared/external_command_parser.h
  src/options/internal.h
  src/options/layout_support.cpp
  src/options/layout_core.cpp
  src/options/layout_folders.cpp
  src/options/layout_editor.cpp
  src/options/apply_pages.cpp
  src/options/apply_pages_runtime.cpp
  src/options/apply_qt.cpp
  src/options/apply_text.cpp
)

set(Z7_UI_FILEMANAGER_DIALOGS_SOURCES
  include/checksum_result_dialog.h
  include/hash_progress_dialog.h
  include/filemanager_task_progress_dialog.h
  src/dialogs/checksum_result/checksum_result_dialog.cpp
  src/dialogs/hash_progress/hash_progress_dialog.cpp
  src/dialogs/task_progress/filemanager_task_progress_dialog.cpp
  src/dialogs/temp_files/temp_files_listing.h
  src/dialogs/temp_files/temp_files_listing.cpp
  src/dialogs/temp_files/temp_files_dialog_model.h
  src/dialogs/temp_files/temp_files_dialog_model.cpp
  src/dialogs/temp_files/temp_files_dialog_service.h
  src/dialogs/temp_files/temp_files_dialog_service.cpp
  src/dialogs/temp_files/temp_files_dialog.h
  src/dialogs/temp_files/temp_files_dialog.cpp
  src/dialogs/temp_files/temp_files_dialog_actions.cpp
)

function(z7_filemanager_configure_object_target target_name)
  target_include_directories(${target_name}
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${PROJECT_SOURCE_DIR}/src/ui/gui/include"
      "${PROJECT_SOURCE_DIR}/src/archive_application/src/runtime"
      "${CMAKE_SOURCE_DIR}/src/third_party/original_7zip/CPP")

  target_link_libraries(${target_name}
    PRIVATE
      z7_app_logic
      z7::common
      z7_ui_common
      z7_ui_archive_support
      z7_ui_runtime_support
      z7_ui_widgets
      Qt6::Core
      Qt6::Concurrent
      Qt6::Widgets
      z7_shell_integration_menu
      z7_platform_qt
      z7_task_ipc_runtime)

  if(APPLE AND TARGET z7_macos_integration_core AND TARGET z7_macos_integration_native_drag)
    target_link_libraries(${target_name}
      PRIVATE
        z7_macos_integration_core
        z7_macos_integration_native_drag)
  endif()

  z7_set_default_target_options(${target_name})
endfunction()

add_library(z7_ui_filemanager_runner_objs OBJECT
  ${Z7_UI_FILEMANAGER_RUNNER_SOURCES})
z7_filemanager_configure_object_target(z7_ui_filemanager_runner_objs)

add_library(z7_ui_filemanager_main_window_objs OBJECT
  ${Z7_UI_FILEMANAGER_MAIN_WINDOW_SOURCES})
z7_filemanager_configure_object_target(z7_ui_filemanager_main_window_objs)
if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_ui_filemanager_main_window_objs
    PRIVATE
      Z7_TESTING)
endif()

add_library(z7_ui_filemanager_options_objs OBJECT
  ${Z7_UI_FILEMANAGER_OPTIONS_SOURCES})
z7_filemanager_configure_object_target(z7_ui_filemanager_options_objs)

add_library(z7_ui_filemanager_dialogs_objs OBJECT
  ${Z7_UI_FILEMANAGER_DIALOGS_SOURCES})
z7_filemanager_configure_object_target(z7_ui_filemanager_dialogs_objs)

add_library(z7_ui_filemanager STATIC
  $<TARGET_OBJECTS:z7_ui_filemanager_runner_objs>
  $<TARGET_OBJECTS:z7_ui_filemanager_main_window_objs>
  $<TARGET_OBJECTS:z7_ui_filemanager_options_objs>
  $<TARGET_OBJECTS:z7_ui_filemanager_dialogs_objs>
)

add_library(z7::ui_filemanager ALIAS z7_ui_filemanager)

target_include_directories(z7_ui_filemanager
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_ui_filemanager
  PUBLIC
    z7_app_logic
    z7_ui_gui
    z7_ui_common
    z7_ui_archive_support
    z7_ui_runtime_support
    z7_ui_widgets
    Qt6::Core
    Qt6::Concurrent
    Qt6::Widgets
  PRIVATE
    z7_shell_integration_menu
    z7_platform_qt
    z7_task_ipc_runtime)

if(APPLE AND TARGET z7_macos_integration_core)
  target_link_libraries(z7_ui_filemanager PUBLIC z7_macos_integration_core)
endif()

if(APPLE AND TARGET z7_macos_integration_native_drag)
  target_link_libraries(z7_ui_filemanager PRIVATE z7_macos_integration_native_drag)
endif()

z7_set_default_target_options(z7_ui_filemanager)
