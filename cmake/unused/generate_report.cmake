include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

z7_unused_prepare_paths()

function(z7_unused_escape_json input_value out_value)
  set(_value "${input_value}")
  string(REPLACE "\\" "\\\\" _value "${_value}")
  string(REPLACE "\"" "\\\"" _value "${_value}")
  string(REPLACE "\n" "\\n" _value "${_value}")
  string(REPLACE "\r" "" _value "${_value}")
  set(${out_value} "${_value}" PARENT_SCOPE)
endfunction()

function(z7_unused_read_record_file file_path out_lines)
  if(EXISTS "${file_path}")
    file(STRINGS "${file_path}" _lines ENCODING UTF-8)
    set(${out_lines} "${_lines}" PARENT_SCOPE)
  else()
    set(${out_lines} "" PARENT_SCOPE)
  endif()
endfunction()

function(z7_unused_parse_record_line line out_ok out_cat out_kind out_symbol out_loc out_evidence out_reasoning out_suggestion)
  string(ASCII 9 _tab)
  string(REPLACE "${_tab}" ";" _parts "${line}")
  list(LENGTH _parts _part_count)

  if(_part_count LESS 7)
    set(${out_ok} FALSE PARENT_SCOPE)
    return()
  endif()

  list(GET _parts 0 _cat)
  list(GET _parts 1 _kind)
  list(GET _parts 2 _symbol)
  list(GET _parts 3 _loc)
  list(GET _parts 4 _evidence)
  list(GET _parts 5 _reasoning)
  list(SUBLIST _parts 6 -1 _suggestion_parts)
  string(JOIN ";" _suggestion ${_suggestion_parts})

  set(${out_ok} TRUE PARENT_SCOPE)
  set(${out_cat} "${_cat}" PARENT_SCOPE)
  set(${out_kind} "${_kind}" PARENT_SCOPE)
  set(${out_symbol} "${_symbol}" PARENT_SCOPE)
  set(${out_loc} "${_loc}" PARENT_SCOPE)
  set(${out_evidence} "${_evidence}" PARENT_SCOPE)
  set(${out_reasoning} "${_reasoning}" PARENT_SCOPE)
  set(${out_suggestion} "${_suggestion}" PARENT_SCOPE)
endfunction()

function(z7_unused_write_markdown_section output_path title lines)
  file(APPEND "${output_path}" "## ${title}\n\n")
  if(NOT lines)
    file(APPEND "${output_path}" "- 无\n\n")
    return()
  endif()

  foreach(_line IN LISTS lines)
    z7_unused_parse_record_line(
      "${_line}" _ok _cat _kind _symbol _loc _evidence _reasoning _suggestion)
    if(NOT _ok)
      continue()
    endif()

    if(_symbol STREQUAL "")
      set(_symbol "(unknown)")
    endif()

    file(APPEND "${output_path}"
      "- symbol: `${_symbol}`; kind: `${_kind}`; location: `${_loc}`\n"
      "  evidence: ${_evidence}\n"
      "  reasoning: ${_reasoning}\n"
      "  suggestion: ${_suggestion}\n")
  endforeach()

  file(APPEND "${output_path}" "\n")
endfunction()

set(_tidy_records "${OUTPUT_DIR}/clang_tidy_records.tsv")
set(_check_records "${OUTPUT_DIR}/clang_check_records.tsv")
set(_func_records "${OUTPUT_DIR}/function_records.tsv")
set(_report_md "${OUTPUT_DIR}/unused_report.md")
set(_report_json "${OUTPUT_DIR}/unused_report.json")

z7_unused_read_record_file("${_tidy_records}" _tidy_lines)
z7_unused_read_record_file("${_check_records}" _check_lines)
z7_unused_read_record_file("${_func_records}" _func_lines)

set(_all_lines ${_tidy_lines} ${_check_lines} ${_func_lines})
set(_unique_keys)
set(_merged_lines)
set(_confirmed_lines)
set(_suspected_lines)
set(_keep_lines)

foreach(_line IN LISTS _all_lines)
  if(_line STREQUAL "")
    continue()
  endif()

  z7_unused_parse_record_line(
    "${_line}" _ok _cat _kind _symbol _loc _evidence _reasoning _suggestion)
  if(NOT _ok)
    continue()
  endif()

  set(_key "${_cat}|${_kind}|${_symbol}|${_loc}")
  list(FIND _unique_keys "${_key}" _existing)
  if(NOT _existing EQUAL -1)
    continue()
  endif()

  list(APPEND _unique_keys "${_key}")
  list(APPEND _merged_lines "${_line}")

  if(_cat STREQUAL "确定未使用")
    list(APPEND _confirmed_lines "${_line}")
  elseif(_cat STREQUAL "疑似未使用")
    list(APPEND _suspected_lines "${_line}")
  else()
    list(APPEND _keep_lines "${_line}")
  endif()
endforeach()

list(SORT _confirmed_lines)
list(SORT _suspected_lines)
list(SORT _keep_lines)

list(LENGTH _merged_lines _total_count)
list(LENGTH _confirmed_lines _confirmed_count)
list(LENGTH _suspected_lines _suspected_count)
list(LENGTH _keep_lines _keep_count)

string(TIMESTAMP _generated_at "%Y-%m-%d %H:%M:%S %z")

file(WRITE "${_report_md}"
  "# Unused Code Analysis Report\n\n"
  "Generated at: ${_generated_at}\n\n"
  "## Summary\n\n"
  "- Total records: ${_total_count}\n"
  "- 确定未使用: ${_confirmed_count}\n"
  "- 疑似未使用: ${_suspected_count}\n"
  "- 暂不应删除: ${_keep_count}\n\n")

z7_unused_write_markdown_section("${_report_md}" "确定未使用" "${_confirmed_lines}")
z7_unused_write_markdown_section("${_report_md}" "疑似未使用" "${_suspected_lines}")
z7_unused_write_markdown_section("${_report_md}" "暂不应删除" "${_keep_lines}")

file(WRITE "${_report_json}" "[\n")
set(_json_index 0)
foreach(_line IN LISTS _merged_lines)
  z7_unused_parse_record_line(
    "${_line}" _ok _cat _kind _symbol _loc _evidence _reasoning _suggestion)
  if(NOT _ok)
    continue()
  endif()

  z7_unused_escape_json("${_cat}" _j_cat)
  z7_unused_escape_json("${_kind}" _j_kind)
  z7_unused_escape_json("${_symbol}" _j_symbol)
  z7_unused_escape_json("${_loc}" _j_loc)
  z7_unused_escape_json("${_evidence}" _j_evidence)
  z7_unused_escape_json("${_reasoning}" _j_reasoning)
  z7_unused_escape_json("${_suggestion}" _j_suggestion)

  if(_json_index GREATER 0)
    file(APPEND "${_report_json}" ",\n")
  endif()

  file(APPEND "${_report_json}"
    "  {\"symbol\":\"${_j_symbol}\",\"kind\":\"${_j_kind}\",\"file\":\"${_j_loc}\",\"category\":\"${_j_cat}\",\"evidence\":\"${_j_evidence}\",\"reasoning\":\"${_j_reasoning}\",\"suggestion\":\"${_j_suggestion}\"}")

  math(EXPR _json_index "${_json_index} + 1")
endforeach()
file(APPEND "${_report_json}" "\n]\n")

message(STATUS "z7_unused_report: wrote ${_report_md}")
message(STATUS "z7_unused_report: wrote ${_report_json}")
