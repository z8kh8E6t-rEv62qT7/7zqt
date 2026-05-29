include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

z7_unused_prepare_paths()
z7_unused_load_first_party_files(_first_party_files)
z7_unused_require_tool("clang-query" _clang_query)
z7_unused_require_tool("rg" _rg)

set(_log_path "${OUTPUT_DIR}/function_candidates.log")
set(_records_path "${OUTPUT_DIR}/function_records.tsv")
set(_query_path "${OUTPUT_DIR}/function_query.cq")

file(WRITE "${_log_path}" "")
file(WRITE "${_records_path}" "")
file(WRITE "${_query_path}" [=[set output dump
match functionDecl(isDefinition(), unless(isExpansionInSystemHeader()), unless(isImplicit())).bind("f")
]=])

set(_seen)
set(_failed_files 0)
set(_processed_files 0)

foreach(_file IN LISTS _first_party_files)
  if(NOT _file MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp)$")
    continue()
  endif()

  math(EXPR _processed_files "${_processed_files} + 1")

  execute_process(
    COMMAND "${_clang_query}"
      -p "${BINARY_DIR}"
      -f "${_query_path}"
      "${_file}"
    RESULT_VARIABLE _query_rc
    OUTPUT_VARIABLE _query_out
    ERROR_VARIABLE _query_err)

  z7_unused_relative_path("${_file}" _rel_file)
  file(APPEND "${_log_path}" "===== ${_rel_file} =====\n${_query_out}\n${_query_err}\n")

  if(NOT _query_rc EQUAL 0)
    math(EXPR _failed_files "${_failed_files} + 1")
    continue()
  endif()

  z7_unused_read_text_lines("${_query_out}" _query_lines)
  foreach(_line IN LISTS _query_lines)
    if(NOT _line MATCHES "^FunctionDecl ")
      continue()
    endif()

    string(REGEX MATCH "<([^>]+)>" _loc_block "${_line}")
    if(_loc_block STREQUAL "")
      continue()
    endif()

    string(REGEX REPLACE "^<([^>]+)>$" "\\1" _loc_inside "${_loc_block}")
    string(REGEX REPLACE "^([^,]+),.*$" "\\1" _start_loc "${_loc_inside}")

    if(NOT _start_loc MATCHES "^(.*):([0-9]+):([0-9]+)$")
      continue()
    endif()

    set(_def_file "${CMAKE_MATCH_1}")
    set(_def_line "${CMAKE_MATCH_2}")
    set(_def_col "${CMAKE_MATCH_3}")

    z7_unused_is_first_party_file("${_def_file}" _is_first_party)
    if(NOT _is_first_party)
      continue()
    endif()

    string(REGEX MATCH "line:[0-9]+:[0-9]+ ([^']+) '" _symbol_block "${_line}")
    if(_symbol_block STREQUAL "")
      continue()
    endif()
    string(REGEX REPLACE "^line:[0-9]+:[0-9]+ ([^']+) '$" "\\1" _symbol "${_symbol_block}")
    string(STRIP "${_symbol}" _symbol)
    if(_symbol STREQUAL "")
      continue()
    endif()

    set(_key "${_def_file}:${_def_line}:${_def_col}:${_symbol}")
    list(FIND _seen "${_key}" _seen_index)
    if(NOT _seen_index EQUAL -1)
      continue()
    endif()
    list(APPEND _seen "${_key}")

    set(_category "疑似未使用")
    set(_kind "function_candidate")
    set(_reasoning "未发现明确调用证据前，按保守策略归为疑似未使用。")
    set(_suggestion "确认无动态调用/反射调用后可删除；若保留请补充调用入口注释。")

    set(_name_for_check "${_symbol}")
    if(_name_for_check MATCHES ".*::([^:]+)$")
      set(_name_for_check "${CMAKE_MATCH_1}")
    endif()

    set(_protect_reason "")
    if(_name_for_check STREQUAL "main")
      set(_protect_reason "main 入口函数")
    elseif(_name_for_check MATCHES "^(qt_.*|qInitResources.*|qCleanupResources.*|qt_static_metacall|qt_metacall|qt_metacast|metaObject)$")
      set(_protect_reason "Qt 元对象/资源入口函数")
    endif()

    set(_ref_count -1)
    if(_protect_reason STREQUAL "" AND _name_for_check MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
      string(MAKE_C_IDENTIFIER "${_name_for_check}" _cache_id)
      set(_cache_var "Z7_UNUSED_REFCOUNT_${_cache_id}")

      if(DEFINED ${_cache_var})
        set(_ref_count "${${_cache_var}}")
      else()
        execute_process(
          COMMAND "${_rg}"
            -n
            --fixed-strings
            --glob "!src/third_party/**"
            --glob "*.h"
            --glob "*.hh"
            --glob "*.hpp"
            --glob "*.c"
            --glob "*.cc"
            --glob "*.cpp"
            --glob "*.cxx"
            "${_name_for_check}"
            src tests
          WORKING_DIRECTORY "${SOURCE_DIR}"
          RESULT_VARIABLE _rg_rc
          OUTPUT_VARIABLE _rg_out
          ERROR_VARIABLE _rg_err)

        if(_rg_rc EQUAL 0)
          z7_unused_read_text_lines("${_rg_out}" _hits)
          set(_count 0)
          foreach(_hit IN LISTS _hits)
            if(NOT _hit STREQUAL "")
              math(EXPR _count "${_count} + 1")
            endif()
          endforeach()
          set(_ref_count "${_count}")
        elseif(_rg_rc EQUAL 1)
          set(_ref_count 0)
        else()
          set(_ref_count -1)
        endif()

        set(${_cache_var} "${_ref_count}")
      endif()
    endif()

    if(NOT _protect_reason STREQUAL "")
      set(_category "暂不应删除")
      set(_kind "function_protected")
      set(_reasoning "${_protect_reason}")
      set(_suggestion "保持现状；如后续确认无用途，再结合调用链复核。")
    elseif(_ref_count GREATER 1)
      set(_category "暂不应删除")
      set(_kind "function_referenced")
      set(_reasoning "名称文本命中次数为 ${_ref_count}，存在调用/引用迹象。")
      set(_suggestion "保持现状；若要删除请先清点真实调用点。")
    elseif(_ref_count EQUAL 1)
      set(_category "疑似未使用")
      set(_kind "function_candidate")
      set(_reasoning "仅检测到 1 处名称命中（通常是定义本身），无明显调用证据。")
      set(_suggestion "复核是否通过回调/反射/宏间接调用；确认无入口后删除。")
    elseif(_ref_count EQUAL 0)
      set(_category "疑似未使用")
      set(_kind "function_candidate")
      set(_reasoning "未检测到名称命中，可能未被调用。")
      set(_suggestion "确认无动态入口后删除，或补充注释说明保留原因。")
    else()
      set(_category "暂不应删除")
      set(_kind "function_uncertain")
      set(_reasoning "无法稳定统计引用命中（符号名复杂或搜索失败）。")
      set(_suggestion "先保留，必要时做手工调用链审查。")
    endif()

    z7_unused_relative_path("${_def_file}" _rel_path)
    set(_location "${_rel_path}:${_def_line}:${_def_col}")
    if(_ref_count GREATER_EQUAL 0)
      set(_evidence "clang-query 提取定义 + rg 命中次数=${_ref_count}")
    else()
      set(_evidence "clang-query 提取定义 + rg 命中次数=unknown")
    endif()

    z7_unused_append_record(
      "${_records_path}"
      "${_category}"
      "${_kind}"
      "${_symbol}"
      "${_location}"
      "${_evidence}"
      "${_reasoning}"
      "${_suggestion}")
  endforeach()
endforeach()

if(_failed_files GREATER 0)
  file(APPEND "${_log_path}"
    "\nclang-query failed for ${_failed_files}/${_processed_files} files.\n")
endif()

message(STATUS "z7_unused_functions: processed ${_processed_files} files")
message(STATUS "z7_unused_functions: wrote ${_log_path}")
message(STATUS "z7_unused_functions: wrote ${_records_path}")
