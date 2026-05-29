include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

z7_unused_prepare_paths()
z7_unused_load_first_party_files(_first_party_files)

z7_unused_require_tool("run-clang-tidy" _run_clang_tidy)

if(NOT _first_party_files)
  message(STATUS "z7_unused_tidy: no first-party files found in compile database")
endif()

set(_log_path "${OUTPUT_DIR}/clang_tidy.log")
set(_records_path "${OUTPUT_DIR}/clang_tidy_records.tsv")
file(WRITE "${_records_path}" "")

z7_unused_escape_regex("${SOURCE_DIR}" _source_regex)
set(_file_regex "^${_source_regex}/(tests|src/(?!third_party/)).*")
set(_checks
  "clang-analyzer-*,misc-unused-*,bugprone-unused-return-value")

set(_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
if("${_jobs}" STREQUAL "")
  set(_jobs "8")
endif()

execute_process(
  COMMAND "${_run_clang_tidy}"
    -quiet
    -p "${BINARY_DIR}"
    -checks=${_checks}
    -j "${_jobs}"
    "${_file_regex}"
  RESULT_VARIABLE _tidy_rc
  OUTPUT_VARIABLE _tidy_out
  ERROR_VARIABLE _tidy_err)

set(_tidy_text "${_tidy_out}\n${_tidy_err}")
file(WRITE "${_log_path}" "${_tidy_text}")

z7_unused_read_text_lines("${_tidy_text}" _tidy_lines)
foreach(_line IN LISTS _tidy_lines)
  if(_line MATCHES "^(.+):([0-9]+):([0-9]+): warning: (.+) \\[(.+)\\]$")
    set(_file "${CMAKE_MATCH_1}")
    set(_line_no "${CMAKE_MATCH_2}")
    set(_col_no "${CMAKE_MATCH_3}")
    set(_message "${CMAKE_MATCH_4}")
    set(_check "${CMAKE_MATCH_5}")

    z7_unused_is_first_party_file("${_file}" _is_first_party)
    if(NOT _is_first_party)
      continue()
    endif()

    set(_kind "")
    set(_suggestion "")

    if(_check STREQUAL "misc-unused-parameters")
      set(_kind "unused_parameter")
      set(_suggestion "删除未使用参数，或显式标记为 unused（例如命名为 _unused 或 Q_UNUSED）。")
    elseif(_check STREQUAL "misc-unused-alias-decls")
      set(_kind "unused_alias")
      set(_suggestion "移除未使用类型别名，或将其移到真正使用处附近。")
    elseif(_check STREQUAL "misc-unused-using-decls")
      set(_kind "unused_using_decl")
      set(_suggestion "删除未使用 using 声明。")
    elseif(_check STREQUAL "bugprone-unused-return-value")
      set(_kind "ignored_return_value")
      set(_suggestion "处理返回值（检查错误码/结果），避免静默忽略。")
    elseif(_check STREQUAL "clang-analyzer-deadcode.DeadStores")
      set(_kind "dead_store")
      set(_suggestion "删除死赋值，或让赋值结果参与后续逻辑。")
    elseif(_check STREQUAL "clang-analyzer-security.insecureAPI.UncheckedReturn")
      set(_kind "ignored_return_value")
      set(_suggestion "补充返回值检查并处理失败路径。")
    endif()

    if(_kind STREQUAL "")
      continue()
    endif()

    set(_symbol "")
    if(_message MATCHES "'([^']+)'")
      set(_symbol "${CMAKE_MATCH_1}")
    endif()

    z7_unused_relative_path("${_file}" _rel_path)
    set(_location "${_rel_path}:${_line_no}:${_col_no}")
    set(_evidence "${_message} [${_check}]")
    set(_reasoning "clang-tidy 直接命中 unused/dead-store/ignored-return 规则。")

    z7_unused_append_record(
      "${_records_path}"
      "确定未使用"
      "${_kind}"
      "${_symbol}"
      "${_location}"
      "${_evidence}"
      "${_reasoning}"
      "${_suggestion}")
  endif()
endforeach()

if(NOT _tidy_rc EQUAL 0)
  file(APPEND "${_log_path}"
    "\nrun-clang-tidy exited with code ${_tidy_rc}. Diagnostics were still collected.\n")
  message(WARNING
    "z7_unused_tidy: run-clang-tidy exited with code ${_tidy_rc}; continuing with collected diagnostics.")
endif()

message(STATUS "z7_unused_tidy: wrote ${_log_path}")
message(STATUS "z7_unused_tidy: wrote ${_records_path}")
