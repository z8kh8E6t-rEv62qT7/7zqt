set(Z7_NATIVE_7Z_SOURCES
  "src/native_7z/backend/benchmark_typed_parser.cpp"
  "src/native_7z/backend/compress_format_catalog.cpp"
  "src/native_7z/backend/native_backend_dispatch.cpp"
  "src/native_7z/backend/native_backend_identity.cpp"
  "src/native_7z/backend/native_request_validation.cpp"

  "src/native_7z/core/core_archive.cpp"
  "src/native_7z/core/core_event_fs.cpp"
  "src/native_7z/core/core_large_pages.cpp"
  "src/native_7z/core/core_parse.cpp"
  "src/native_7z/core/entry.cpp"
  "src/native_7z/core/event.cpp"
  "src/native_7z/core/event_hash.cpp"
  "src/native_7z/core/event_list.cpp"
  "src/native_7z/core/event_open.cpp"
  "src/native_7z/core/event_update.cpp"
  "src/native_7z/core/filesystem_replace.cpp"
  "src/native_7z/core/internal_raii.cpp"

  "src/native_7z/callbacks/callbacks_extract_memory.cpp"
  "src/native_7z/callbacks/callbacks_extract_com.cpp"
  "src/native_7z/callbacks/callbacks_extract_errors.cpp"
  "src/native_7z/callbacks/callbacks_extract_get_stream.cpp"
  "src/native_7z/callbacks/callbacks_extract_password.cpp"
  "src/native_7z/callbacks/callbacks_extract_paths.cpp"
  "src/native_7z/callbacks/callbacks_extract_progress.cpp"
  "src/native_7z/callbacks/callbacks_extract_progress_run.cpp"
  "src/native_7z/callbacks/callbacks_extract_result.cpp"
  "src/native_7z/callbacks/callbacks_extract_run.cpp"
  "src/native_7z/callbacks/callbacks_extract_stream.cpp"
  "src/native_7z/callbacks/callbacks_extract_test.cpp"
  "src/native_7z/callbacks/callbacks_update_benchmark.cpp"
  "src/native_7z/callbacks/callbacks_update_operation_core.cpp"
  "src/native_7z/callbacks/callbacks_update_operation_flow.cpp"
  "src/native_7z/callbacks/callbacks_update_operation_open.cpp"

  "src/native_7z/operations/extract_output.cpp"
  "src/native_7z/operations/operations_add.cpp"
  "src/native_7z/operations/operations_add_input_items.cpp"
  "src/native_7z/operations/operations_benchmark_volume.cpp"
  "src/native_7z/operations/operations_extract.cpp"
  "src/native_7z/operations/operations_test_hash_delete.cpp"
  "src/native_7z/operations/operations_update_add_runtime.cpp"

  "src/native_7z/info/info_comment.cpp"
  "src/native_7z/info/info_entry_info.cpp"
  "src/native_7z/info/info_fs_ops.cpp"
  "src/native_7z/info/info_hash.cpp"
  "src/native_7z/info/info_properties.cpp"
  "src/native_7z/info/info_properties_extract_archive.cpp"
  "src/native_7z/info/info_properties_extract_common.cpp"
  "src/native_7z/info/info_properties_extract_context.cpp"
  "src/native_7z/info/info_properties_extract_selected.cpp"
  "src/native_7z/info/info_properties_extract_summary.cpp"
  "src/native_7z/info/info_properties_format.cpp"
  "src/native_7z/info/info_runtime.cpp"

  "src/native_7z/session/session_registry.cpp"
  "src/native_7z/session/session_registry_open_parent.cpp"
  "src/native_7z/session/session_registry_open_path.cpp")

set(Z7_APP_LOGIC_SOURCES
  src/runtime/archive_session.cpp
  src/runtime/archive_session_core.cpp
  src/runtime/archive_session_event_sink.cpp
  src/runtime/archive_session_helpers.cpp
  src/runtime/archive_session_interactions.cpp
  src/runtime/archive_session_memory_gate.cpp
  src/support/archive_error.cpp
  src/support/descript_ion_store.cpp
  ${Z7_NATIVE_7Z_SOURCES}
)

add_library(z7_app_logic STATIC
  ${Z7_APP_LOGIC_SOURCES}
)

add_library(z7::app_logic ALIAS z7_app_logic)

target_include_directories(z7_app_logic
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_7z"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${Z7_ORIGINAL_VENDOR_DIR}"
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP")

target_link_libraries(z7_app_logic
  PRIVATE
    z7::common
    z7::third_party)

z7_set_default_target_options(z7_app_logic)

if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_app_logic
    PUBLIC
      Z7_TESTING)
endif()

add_library(z7_original_static_runtime INTERFACE)
add_library(z7::original_static_runtime ALIAS z7_original_static_runtime)
target_link_libraries(z7_original_static_runtime
  INTERFACE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_core_common>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_archive_api>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_agent_api>")
if(APPLE)
  target_link_libraries(z7_original_static_runtime
    INTERFACE
      "-framework CoreFoundation"
      iconv)
endif()

add_library(z7_app_logic_static_original STATIC
  ${Z7_APP_LOGIC_SOURCES}
)

add_library(z7::app_logic_static_original ALIAS z7_app_logic_static_original)

target_include_directories(z7_app_logic_static_original
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_7z"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${Z7_ORIGINAL_VENDOR_DIR}"
    "${Z7_ORIGINAL_VENDOR_DIR}/CPP")

target_link_libraries(z7_app_logic_static_original
  PRIVATE
    z7::common
  PUBLIC
    z7::original_static_runtime)

z7_set_default_target_options(z7_app_logic_static_original)

if(Z7_BUILD_TESTS)
  target_compile_definitions(z7_app_logic_static_original
    PUBLIC
      Z7_TESTING)
endif()
