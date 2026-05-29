# Shared helpers for the unused-code analysis pipeline.

function(z7_unused_require_var name)
  if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
    message(FATAL_ERROR "z7_unused: missing required variable: ${name}")
  endif()
endfunction()

function(z7_unused_require_tool tool_name out_var)
  find_program(_tool_path NAMES "${tool_name}")
  if(NOT _tool_path)
    message(FATAL_ERROR "z7_unused: required tool not found: ${tool_name}")
  endif()
  set(${out_var} "${_tool_path}" PARENT_SCOPE)
endfunction()

function(z7_unused_prepare_paths)
  z7_unused_require_var(SOURCE_DIR)
  z7_unused_require_var(BINARY_DIR)
  z7_unused_require_var(OUTPUT_DIR)

  cmake_path(NORMAL_PATH SOURCE_DIR OUTPUT_VARIABLE _source_dir)
  cmake_path(NORMAL_PATH BINARY_DIR OUTPUT_VARIABLE _binary_dir)
  cmake_path(NORMAL_PATH OUTPUT_DIR OUTPUT_VARIABLE _output_dir)

  set(SOURCE_DIR "${_source_dir}" PARENT_SCOPE)
  set(BINARY_DIR "${_binary_dir}" PARENT_SCOPE)
  set(OUTPUT_DIR "${_output_dir}" PARENT_SCOPE)

  set(COMPILE_COMMANDS_PATH "${_binary_dir}/compile_commands.json" PARENT_SCOPE)
  file(MAKE_DIRECTORY "${_output_dir}")
endfunction()

function(z7_unused_is_first_party_file input_path out_var)
  cmake_path(NORMAL_PATH input_path OUTPUT_VARIABLE _path)
  string(REPLACE "${SOURCE_DIR}/" "" _rel "${_path}")

  if(_rel STREQUAL "${_path}")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()

  if(_rel MATCHES "^(src|tests)/" AND NOT _rel MATCHES "^src/third_party/")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(z7_unused_load_first_party_files out_list)
  z7_unused_prepare_paths()

  if(NOT EXISTS "${COMPILE_COMMANDS_PATH}")
    message(FATAL_ERROR
      "z7_unused: compile_commands.json is missing at ${COMPILE_COMMANDS_PATH}. "
      "Run cmake configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON.")
  endif()

  file(READ "${COMPILE_COMMANDS_PATH}" _json)
  string(JSON _entry_count LENGTH "${_json}")

  set(_files)
  if(_entry_count GREATER 0)
    math(EXPR _last_index "${_entry_count} - 1")
    foreach(_index RANGE 0 ${_last_index})
      string(JSON _file GET "${_json}" ${_index} file)
      z7_unused_is_first_party_file("${_file}" _is_first_party)
      if(_is_first_party)
        list(APPEND _files "${_file}")
      endif()
    endforeach()
  endif()

  list(REMOVE_DUPLICATES _files)
  list(SORT _files)
  set(${out_list} "${_files}" PARENT_SCOPE)
endfunction()

function(z7_unused_escape_field input_value out_value)
  set(_value "${input_value}")
  string(REPLACE "\\" "\\\\" _value "${_value}")
  string(REPLACE "\t" " " _value "${_value}")
  string(REPLACE "\n" " " _value "${_value}")
  string(REPLACE "\r" "" _value "${_value}")
  string(REPLACE ";" ", " _value "${_value}")
  set(${out_value} "${_value}" PARENT_SCOPE)
endfunction()

function(z7_unused_escape_regex input_value out_value)
  set(_value "${input_value}")
  string(REGEX REPLACE "([][+.*^$(){}|\\\\?])" "\\\\\\1" _value "${_value}")
  set(${out_value} "${_value}" PARENT_SCOPE)
endfunction()

function(z7_unused_append_record file_path category kind symbol location evidence reasoning suggestion)
  z7_unused_escape_field("${category}" _category)
  z7_unused_escape_field("${kind}" _kind)
  z7_unused_escape_field("${symbol}" _symbol)
  z7_unused_escape_field("${location}" _location)
  z7_unused_escape_field("${evidence}" _evidence)
  z7_unused_escape_field("${reasoning}" _reasoning)
  z7_unused_escape_field("${suggestion}" _suggestion)

  file(APPEND "${file_path}"
    "${_category}\t${_kind}\t${_symbol}\t${_location}\t${_evidence}\t${_reasoning}\t${_suggestion}\n")
endfunction()

function(z7_unused_read_text_lines text out_lines)
  string(REPLACE "\r\n" "\n" _normalized "${text}")
  string(REPLACE "\r" "\n" _normalized "${_normalized}")
  string(REPLACE "\n" ";" _lines "${_normalized}")
  set(${out_lines} "${_lines}" PARENT_SCOPE)
endfunction()

function(z7_unused_relative_path input_path out_path)
  cmake_path(NORMAL_PATH input_path OUTPUT_VARIABLE _path)
  string(REPLACE "${SOURCE_DIR}/" "" _rel "${_path}")
  if(_rel STREQUAL "${_path}")
    set(${out_path} "${_path}" PARENT_SCOPE)
  else()
    set(${out_path} "${_rel}" PARENT_SCOPE)
  endif()
endfunction()
