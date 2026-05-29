include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

z7_unused_prepare_paths()
z7_unused_load_first_party_files(_first_party_files)
z7_unused_require_tool("clang-check" _clang_check)

set(_log_path "${OUTPUT_DIR}/clang_check_unused.log")
set(_records_path "${OUTPUT_DIR}/clang_check_records.tsv")
file(WRITE "${_log_path}" "")
file(WRITE "${_records_path}" "")

set(_failed_count 0)
set(_processed_count 0)

foreach(_file IN LISTS _first_party_files)
  math(EXPR _processed_count "${_processed_count} + 1")

  execute_process(
    COMMAND "${_clang_check}"
      -p "${BINARY_DIR}"
      --extra-arg=-Wall
      --extra-arg=-Wextra
      --extra-arg=-Wunused
      "${_file}"
    RESULT_VARIABLE _check_rc
    OUTPUT_VARIABLE _check_out
    ERROR_VARIABLE _check_err)

  set(_text "${_check_out}\n${_check_err}")
  if(NOT "${_text}" STREQUAL "\n")
    z7_unused_relative_path("${_file}" _rel_file)
    file(APPEND "${_log_path}" "===== ${_rel_file} =====\n${_text}\n")
  endif()

  if(NOT _check_rc EQUAL 0)
    math(EXPR _failed_count "${_failed_count} + 1")
  endif()

  z7_unused_read_text_lines("${_text}" _lines)
  foreach(_line IN LISTS _lines)
    set(_kind "")
    set(_suggestion "")
    set(_flag "")

    if(_line MATCHES "^(.+):([0-9]+):([0-9]+): warning: (.+) \\[(-W[^]]+)\\]$")
      set(_diag_file "${CMAKE_MATCH_1}")
      set(_diag_line "${CMAKE_MATCH_2}")
      set(_diag_col "${CMAKE_MATCH_3}")
      set(_message "${CMAKE_MATCH_4}")
      set(_flag "${CMAKE_MATCH_5}")
    elseif(_line MATCHES "^(.+):([0-9]+):([0-9]+): warning: (.+)$")
      set(_diag_file "${CMAKE_MATCH_1}")
      set(_diag_line "${CMAKE_MATCH_2}")
      set(_diag_col "${CMAKE_MATCH_3}")
      set(_message "${CMAKE_MATCH_4}")
    else()
      continue()
    endif()

    z7_unused_is_first_party_file("${_diag_file}" _is_first_party)
    if(NOT _is_first_party)
      continue()
    endif()

    if(_flag STREQUAL "-Wunused-variable")
      set(_kind "unused_variable")
      set(_suggestion "删除未使用变量，或将其用于实际逻辑。")
    elseif(_flag STREQUAL "-Wunused-but-set-variable")
      set(_kind "dead_store")
      set(_suggestion "删除仅赋值未读取的变量，或补齐读取逻辑。")
    elseif(_flag STREQUAL "-Wunused-parameter")
      set(_kind "unused_parameter")
      set(_suggestion "移除未使用参数，或在接口约束下显式标记 unused。")
    elseif(_flag STREQUAL "-Wunused-local-typedef" OR _flag STREQUAL "-Wunused-local-typedefs")
      set(_kind "unused_alias")
      set(_suggestion "删除未使用局部类型别名。")
    elseif(_flag STREQUAL "-Wunused-function")
      set(_kind "unused_function")
      set(_suggestion "删除未使用静态函数，或若需保留请补齐调用路径。")
    elseif(_flag STREQUAL "-Wunused-result")
      set(_kind "ignored_return_value")
      set(_suggestion "处理返回值并补齐错误路径。")
    elseif(_flag MATCHES "^-Wunused")
      set(_kind "unused_symbol")
      set(_suggestion "清理未使用符号，避免无效代码积累。")
    elseif(_message MATCHES "ignored return value" OR _message MATCHES "unused result")
      set(_kind "ignored_return_value")
      set(_suggestion "处理返回值并补齐错误路径。")
    endif()

    if(_kind STREQUAL "")
      continue()
    endif()

    set(_symbol "")
    if(_message MATCHES "'([^']+)'")
      set(_symbol "${CMAKE_MATCH_1}")
    endif()

    z7_unused_relative_path("${_diag_file}" _rel_path)
    set(_location "${_rel_path}:${_diag_line}:${_diag_col}")

    if("${_flag}" STREQUAL "")
      set(_evidence "${_message}")
    else()
      set(_evidence "${_message} [${_flag}]")
    endif()

    z7_unused_append_record(
      "${_records_path}"
      "确定未使用"
      "${_kind}"
      "${_symbol}"
      "${_location}"
      "${_evidence}"
      "编译器 unused 告警直接命中。"
      "${_suggestion}")
  endforeach()
endforeach()

if(_failed_count GREATER 0)
  file(APPEND "${_log_path}"
    "\nclang-check returned non-zero for ${_failed_count}/${_processed_count} files.\n")
endif()

message(STATUS "z7_unused_warnings: processed ${_processed_count} files")
message(STATUS "z7_unused_warnings: wrote ${_log_path}")
message(STATUS "z7_unused_warnings: wrote ${_records_path}")
