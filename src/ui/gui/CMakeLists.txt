add_library(z7_ui_gui STATIC
  include/benchmark_dialog.h
  include/compress_dialog.h
  include/dialog_command_options.h
  include/extract_dialog.h
  include/gui_app_controller.h
  include/gui_task_runner.h
  include/gui_task_spec_ipc.h
  include/hash_result_dialog.h
  include/gui_task_progress_dialog.h
  include/task_cancellation.h
  src/gui_app_controller/helpers.h
  src/gui_app_controller/helpers.cpp
  src/gui_app_controller/test_result.cpp
  src/gui_app_controller/gui_app_controller.cpp
  src/gui_task_spec_ipc.cpp
  src/benchmark/layout_helpers.cpp
  src/benchmark/layout_build.cpp
  src/benchmark/layout.cpp
  src/benchmark/metric.cpp
  src/compress/layout_rules.cpp
  src/compress/layout_memory.cpp
  src/compress/bind_rebuild.cpp
  src/compress/layout.cpp
  src/compress/options.cpp
  src/compress/bind.cpp
  src/extract_dialog.cpp
  src/gui_task_runner/internal.h
  src/gui_task_runner/shared.h
  src/gui_task_runner/shared.cpp
  src/gui_task_runner/async.cpp
  src/gui_task_runner/runner.cpp
  src/gui_task_runner_helpers.cpp
  src/gui_task_runner_helpers_request.cpp
  src/gui_task_runner_helpers_dialogs.cpp
  src/hash_result_dialog.cpp
  src/gui_task_progress_dialog.cpp
)

add_library(z7::ui_gui ALIAS z7_ui_gui)

target_include_directories(z7_ui_gui
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_ui_gui
  PUBLIC
    z7_app_logic
    z7_ui_common
    z7_task_ipc_runtime
    z7_ui_archive_support
    z7_ui_runtime_support
    Qt6::Concurrent
    Qt6::Widgets
  PRIVATE
    z7::common
    z7::shell_integration_menu
    z7_platform_qt)

z7_set_default_target_options(z7_ui_gui)

if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_ui_gui
    PUBLIC
      Z7_TESTING)
endif()
