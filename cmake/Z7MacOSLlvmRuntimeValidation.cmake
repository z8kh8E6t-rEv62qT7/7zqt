include_guard(GLOBAL)

function(_z7_macos_runtime_find_otool output_variable)
  if(DEFINED Z7_OTOOL_EXECUTABLE AND NOT "${Z7_OTOOL_EXECUTABLE}" STREQUAL "")
    set(${output_variable} "${Z7_OTOOL_EXECUTABLE}" PARENT_SCOPE)
    return()
  endif()

  find_program(_z7_macos_runtime_otool
    NAMES otool
    REQUIRED)
  set(${output_variable} "${_z7_macos_runtime_otool}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_load_output file_path output_variable)
  _z7_macos_runtime_find_otool(_z7_macos_runtime_otool)
  execute_process(
    COMMAND "${_z7_macos_runtime_otool}" -L "${file_path}"
    RESULT_VARIABLE _z7_macos_runtime_otool_result
    OUTPUT_VARIABLE _z7_macos_runtime_otool_output
    ERROR_VARIABLE _z7_macos_runtime_otool_error)

  if(NOT _z7_macos_runtime_otool_result EQUAL 0)
    set(${output_variable} "" PARENT_SCOPE)
    set(${output_variable}_IS_MACHO FALSE PARENT_SCOPE)
    set(${output_variable}_ERROR "${_z7_macos_runtime_otool_error}" PARENT_SCOPE)
    return()
  endif()

  set(${output_variable} "${_z7_macos_runtime_otool_output}" PARENT_SCOPE)
  set(${output_variable}_IS_MACHO TRUE PARENT_SCOPE)
  set(${output_variable}_ERROR "" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_collect_load_paths output_variable text)
  set(_z7_macos_runtime_load_paths "")
  string(REPLACE "\n" ";" _z7_macos_runtime_lines "${text}")
  foreach(_z7_macos_runtime_line IN LISTS _z7_macos_runtime_lines)
    string(STRIP "${_z7_macos_runtime_line}" _z7_macos_runtime_line)
    if(_z7_macos_runtime_line STREQUAL "" OR
       _z7_macos_runtime_line MATCHES ":$")
      continue()
    endif()
    string(REGEX REPLACE " \\(.*$" "" _z7_macos_runtime_load_path
      "${_z7_macos_runtime_line}")
    list(APPEND _z7_macos_runtime_load_paths
      "${_z7_macos_runtime_load_path}")
  endforeach()
  set(${output_variable} "${_z7_macos_runtime_load_paths}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_collect_system_violations output_variable)
  set(_z7_macos_runtime_violations "")
  foreach(_z7_macos_runtime_load_path IN LISTS ARGN)
    string(FIND "${_z7_macos_runtime_load_path}" "/usr/lib/libc++abi"
      _z7_macos_runtime_libcxxabi_index)
    string(FIND "${_z7_macos_runtime_load_path}" "/usr/lib/libc++"
      _z7_macos_runtime_libcxx_index)
    string(FIND "${_z7_macos_runtime_load_path}" "/usr/lib/libunwind"
      _z7_macos_runtime_libunwind_index)
    if(_z7_macos_runtime_libcxxabi_index EQUAL 0 OR
       _z7_macos_runtime_libcxx_index EQUAL 0 OR
       _z7_macos_runtime_libunwind_index EQUAL 0)
      list(APPEND _z7_macos_runtime_violations
        "system runtime: ${_z7_macos_runtime_load_path}")
    endif()
  endforeach()
  set(${output_variable} "${_z7_macos_runtime_violations}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_collect_absolute_homebrew_violations output_variable llvm_root)
  set(_z7_macos_runtime_violations "")
  set(_z7_macos_runtime_roots "")
  if(NOT "${llvm_root}" STREQUAL "")
    list(APPEND _z7_macos_runtime_roots "${llvm_root}")
  endif()
  list(APPEND _z7_macos_runtime_roots
    "/opt/homebrew/opt/llvm"
    "/usr/local/opt/llvm")
  list(REMOVE_DUPLICATES _z7_macos_runtime_roots)

  foreach(_z7_macos_runtime_load_path IN LISTS ARGN)
    set(_z7_macos_runtime_is_absolute_homebrew_runtime FALSE)
    foreach(_z7_macos_runtime_root IN LISTS _z7_macos_runtime_roots)
      foreach(_z7_macos_runtime_prefix IN ITEMS
          "${_z7_macos_runtime_root}/lib/c++/libc++abi"
          "${_z7_macos_runtime_root}/lib/c++/libc++"
          "${_z7_macos_runtime_root}/lib/unwind/libunwind")
        string(FIND
          "${_z7_macos_runtime_load_path}"
          "${_z7_macos_runtime_prefix}"
          _z7_macos_runtime_prefix_index)
        if(_z7_macos_runtime_prefix_index EQUAL 0)
          set(_z7_macos_runtime_is_absolute_homebrew_runtime TRUE)
        endif()
      endforeach()
    endforeach()

    foreach(_z7_macos_runtime_cellar_root IN ITEMS
        "/opt/homebrew/Cellar/llvm/"
        "/usr/local/Cellar/llvm/")
      string(FIND
        "${_z7_macos_runtime_load_path}"
        "${_z7_macos_runtime_cellar_root}"
        _z7_macos_runtime_cellar_index)
      if(_z7_macos_runtime_cellar_index EQUAL 0)
        foreach(_z7_macos_runtime_fragment IN ITEMS
            "/lib/c++/libc++abi"
            "/lib/c++/libc++"
            "/lib/unwind/libunwind")
          string(FIND
            "${_z7_macos_runtime_load_path}"
            "${_z7_macos_runtime_fragment}"
            _z7_macos_runtime_fragment_index)
          if(NOT _z7_macos_runtime_fragment_index EQUAL -1)
            set(_z7_macos_runtime_is_absolute_homebrew_runtime TRUE)
          endif()
        endforeach()
      endif()
    endforeach()

    if(_z7_macos_runtime_is_absolute_homebrew_runtime)
      list(APPEND _z7_macos_runtime_violations
        "absolute Homebrew runtime: ${_z7_macos_runtime_load_path}")
    endif()
  endforeach()

  set(${output_variable} "${_z7_macos_runtime_violations}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_dylib_name_is_llvm_runtime output_variable load_path)
  get_filename_component(_z7_macos_runtime_load_name "${load_path}" NAME)
  if(_z7_macos_runtime_load_name STREQUAL "libc++.1.dylib" OR
     _z7_macos_runtime_load_name STREQUAL "libc++abi.1.dylib" OR
     _z7_macos_runtime_load_name STREQUAL "libc++abi.dylib" OR
     _z7_macos_runtime_load_name STREQUAL "libunwind.1.dylib" OR
     _z7_macos_runtime_load_name STREQUAL "libunwind.dylib")
    set(${output_variable} TRUE PARENT_SCOPE)
  else()
    set(${output_variable} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_z7_macos_runtime_canonical_runtime_name output_variable load_path)
  get_filename_component(_z7_macos_runtime_load_name "${load_path}" NAME)
  if(_z7_macos_runtime_load_name STREQUAL "libc++abi.dylib")
    set(${output_variable} "libc++abi.1.dylib" PARENT_SCOPE)
  elseif(_z7_macos_runtime_load_name STREQUAL "libunwind.dylib")
    set(${output_variable} "libunwind.1.dylib" PARENT_SCOPE)
  else()
    set(${output_variable} "${_z7_macos_runtime_load_name}" PARENT_SCOPE)
  endif()
endfunction()

function(_z7_macos_runtime_normalize_path output_variable path_value)
  cmake_path(ABSOLUTE_PATH path_value NORMALIZE OUTPUT_VARIABLE _z7_macos_runtime_normalized)
  set(${output_variable} "${_z7_macos_runtime_normalized}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_resolve_macro_path output_variable path_value file_path contents_dir)
  get_filename_component(_z7_macos_runtime_real_file_path
    "${file_path}"
    REALPATH)
  get_filename_component(
    _z7_macos_runtime_real_file_dir
    "${_z7_macos_runtime_real_file_path}"
    DIRECTORY)
  set(_z7_macos_runtime_macos_dir "${contents_dir}/MacOS")

  string(FIND "${path_value}" "@loader_path/" _z7_macos_runtime_loader_index)
  string(FIND "${path_value}" "@executable_path/" _z7_macos_runtime_executable_index)
  if(_z7_macos_runtime_loader_index EQUAL 0)
    string(LENGTH "@loader_path/" _z7_macos_runtime_prefix_length)
    string(SUBSTRING
      "${path_value}"
      ${_z7_macos_runtime_prefix_length}
      -1
      _z7_macos_runtime_suffix)
    _z7_macos_runtime_normalize_path(
      _z7_macos_runtime_resolved
      "${_z7_macos_runtime_real_file_dir}/${_z7_macos_runtime_suffix}")
    set(${output_variable} "${_z7_macos_runtime_resolved}" PARENT_SCOPE)
  elseif(_z7_macos_runtime_executable_index EQUAL 0)
    string(LENGTH "@executable_path/" _z7_macos_runtime_prefix_length)
    string(SUBSTRING
      "${path_value}"
      ${_z7_macos_runtime_prefix_length}
      -1
      _z7_macos_runtime_suffix)
    _z7_macos_runtime_normalize_path(
      _z7_macos_runtime_resolved
      "${_z7_macos_runtime_macos_dir}/${_z7_macos_runtime_suffix}")
    set(${output_variable} "${_z7_macos_runtime_resolved}" PARENT_SCOPE)
  elseif(IS_ABSOLUTE "${path_value}")
    _z7_macos_runtime_normalize_path(
      _z7_macos_runtime_resolved
      "${path_value}")
    set(${output_variable} "${_z7_macos_runtime_resolved}" PARENT_SCOPE)
  else()
    set(${output_variable} "" PARENT_SCOPE)
  endif()
endfunction()

function(_z7_macos_runtime_collect_rpaths output_variable file_path)
  _z7_macos_runtime_find_otool(_z7_macos_runtime_otool)
  execute_process(
    COMMAND "${_z7_macos_runtime_otool}" -l "${file_path}"
    RESULT_VARIABLE _z7_macos_runtime_otool_result
    OUTPUT_VARIABLE _z7_macos_runtime_otool_output
    ERROR_QUIET)
  if(NOT _z7_macos_runtime_otool_result EQUAL 0)
    set(${output_variable} "" PARENT_SCOPE)
    return()
  endif()

  set(_z7_macos_runtime_rpaths "")
  set(_z7_macos_runtime_in_rpath FALSE)
  string(REPLACE "\n" ";" _z7_macos_runtime_lines
    "${_z7_macos_runtime_otool_output}")
  foreach(_z7_macos_runtime_line IN LISTS _z7_macos_runtime_lines)
    string(STRIP "${_z7_macos_runtime_line}" _z7_macos_runtime_line)
    if(_z7_macos_runtime_line STREQUAL "cmd LC_RPATH")
      set(_z7_macos_runtime_in_rpath TRUE)
    elseif(_z7_macos_runtime_in_rpath AND
           _z7_macos_runtime_line MATCHES "^path (.*) \\(offset [0-9]+\\)$")
      list(APPEND _z7_macos_runtime_rpaths "${CMAKE_MATCH_1}")
      set(_z7_macos_runtime_in_rpath FALSE)
    elseif(_z7_macos_runtime_line MATCHES "^cmd ")
      set(_z7_macos_runtime_in_rpath FALSE)
    endif()
  endforeach()

  list(REMOVE_DUPLICATES _z7_macos_runtime_rpaths)
  set(${output_variable} "${_z7_macos_runtime_rpaths}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_resolve_bundle_runtime_load output_variable load_path file_path contents_dir)
  string(FIND "${load_path}" "@rpath/" _z7_macos_runtime_rpath_index)
  if(_z7_macos_runtime_rpath_index EQUAL 0)
    string(LENGTH "@rpath/" _z7_macos_runtime_prefix_length)
    string(SUBSTRING
      "${load_path}"
      ${_z7_macos_runtime_prefix_length}
      -1
      _z7_macos_runtime_suffix)
    _z7_macos_runtime_collect_rpaths(
      _z7_macos_runtime_rpaths
      "${file_path}")
    foreach(_z7_macos_runtime_rpath IN LISTS _z7_macos_runtime_rpaths)
      _z7_macos_runtime_resolve_macro_path(
        _z7_macos_runtime_resolved_rpath
        "${_z7_macos_runtime_rpath}"
        "${file_path}"
        "${contents_dir}")
      if(_z7_macos_runtime_resolved_rpath STREQUAL "")
        continue()
      endif()
      _z7_macos_runtime_normalize_path(
        _z7_macos_runtime_candidate
        "${_z7_macos_runtime_resolved_rpath}/${_z7_macos_runtime_suffix}")
      if(EXISTS "${_z7_macos_runtime_candidate}")
        set(${output_variable} "${_z7_macos_runtime_candidate}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
    set(${output_variable} "" PARENT_SCOPE)
    return()
  endif()

  _z7_macos_runtime_resolve_macro_path(
    _z7_macos_runtime_resolved
    "${load_path}"
    "${file_path}"
    "${contents_dir}")
  set(${output_variable} "${_z7_macos_runtime_resolved}" PARENT_SCOPE)
endfunction()

function(_z7_macos_runtime_validate_bundle_load_targets file_path contents_dir)
  _z7_macos_runtime_load_output(
    "${file_path}"
    _z7_macos_runtime_output)
  if(NOT _z7_macos_runtime_output_IS_MACHO)
    return()
  endif()

  set(_z7_macos_runtime_frameworks_dir "${contents_dir}/Frameworks")
  _z7_macos_runtime_collect_load_paths(
    _z7_macos_runtime_load_paths
    "${_z7_macos_runtime_output}")
  foreach(_z7_macos_runtime_load_path IN LISTS _z7_macos_runtime_load_paths)
    _z7_macos_runtime_dylib_name_is_llvm_runtime(
      _z7_macos_runtime_is_llvm_runtime
      "${_z7_macos_runtime_load_path}")
    if(NOT _z7_macos_runtime_is_llvm_runtime)
      continue()
    endif()

    _z7_macos_runtime_canonical_runtime_name(
      _z7_macos_runtime_expected_name
      "${_z7_macos_runtime_load_path}")
    _z7_macos_runtime_normalize_path(
      _z7_macos_runtime_expected_path
      "${_z7_macos_runtime_frameworks_dir}/${_z7_macos_runtime_expected_name}")
    _z7_macos_runtime_resolve_bundle_runtime_load(
      _z7_macos_runtime_resolved_path
      "${_z7_macos_runtime_load_path}"
      "${file_path}"
      "${contents_dir}")

    if(_z7_macos_runtime_resolved_path STREQUAL "")
      message(FATAL_ERROR
        "Cannot resolve bundled LLVM runtime load in ${file_path}:\n"
        "  ${_z7_macos_runtime_load_path}\n"
        "${_z7_macos_runtime_output}")
    endif()
    if(NOT _z7_macos_runtime_resolved_path STREQUAL _z7_macos_runtime_expected_path)
      message(FATAL_ERROR
        "Bundled LLVM runtime load resolves outside Contents/Frameworks in "
        "${file_path}:\n"
        "  load: ${_z7_macos_runtime_load_path}\n"
        "  resolved: ${_z7_macos_runtime_resolved_path}\n"
        "  expected: ${_z7_macos_runtime_expected_path}\n"
        "${_z7_macos_runtime_output}")
    endif()
    if(NOT EXISTS "${_z7_macos_runtime_resolved_path}")
      message(FATAL_ERROR
        "Bundled LLVM runtime load target is missing for ${file_path}:\n"
        "  load: ${_z7_macos_runtime_load_path}\n"
        "  resolved: ${_z7_macos_runtime_resolved_path}\n"
        "${_z7_macos_runtime_output}")
    endif()
  endforeach()
endfunction()

function(_z7_macos_runtime_require_expected_loads file_path)
  set(_z7_macos_runtime_missing "")
  foreach(_z7_macos_runtime_name IN ITEMS
      "libc++.1.dylib"
      "libc++abi.1.dylib"
      "libunwind.1.dylib")
    set(_z7_macos_runtime_found FALSE)
    foreach(_z7_macos_runtime_load_path IN LISTS ARGN)
      string(FIND
        "${_z7_macos_runtime_load_path}"
        "${_z7_macos_runtime_name}"
        _z7_macos_runtime_found_index)
      if(NOT _z7_macos_runtime_found_index EQUAL -1)
        set(_z7_macos_runtime_found TRUE)
      endif()
    endforeach()
    if(NOT _z7_macos_runtime_found)
      list(APPEND _z7_macos_runtime_missing "${_z7_macos_runtime_name}")
    endif()
  endforeach()

  if(_z7_macos_runtime_missing)
    string(REPLACE ";" ", " _z7_macos_runtime_missing_text
      "${_z7_macos_runtime_missing}")
    message(FATAL_ERROR
      "Release target does not load expected Homebrew LLVM runtimes "
      "(${_z7_macos_runtime_missing_text}): ${file_path}")
  endif()
endfunction()

function(z7_macos_validate_llvm_runtime_file file_path)
  set(_z7_macos_runtime_options
    REQUIRE_LLVM_RUNTIME
    FORBID_ABSOLUTE_HOMEBREW_RUNTIME
    ALLOW_NON_MACHO)
  set(_z7_macos_runtime_one_value LLVM_ROOT)
  cmake_parse_arguments(
    Z7_VALIDATE
    "${_z7_macos_runtime_options}"
    "${_z7_macos_runtime_one_value}"
    ""
    ${ARGN})

  _z7_macos_runtime_load_output(
    "${file_path}"
    _z7_macos_runtime_output)
  if(NOT _z7_macos_runtime_output_IS_MACHO)
    if(Z7_VALIDATE_ALLOW_NON_MACHO)
      return()
    endif()
    message(FATAL_ERROR
      "otool -L failed for ${file_path}:\n"
      "${_z7_macos_runtime_output_ERROR}")
  endif()

  _z7_macos_runtime_collect_load_paths(
    _z7_macos_runtime_load_paths
    "${_z7_macos_runtime_output}")
  _z7_macos_runtime_collect_system_violations(
    _z7_macos_runtime_violations
    ${_z7_macos_runtime_load_paths})
  if(Z7_VALIDATE_FORBID_ABSOLUTE_HOMEBREW_RUNTIME)
    _z7_macos_runtime_collect_absolute_homebrew_violations(
      _z7_macos_runtime_homebrew_violations
      "${Z7_VALIDATE_LLVM_ROOT}"
      ${_z7_macos_runtime_load_paths})
    list(APPEND _z7_macos_runtime_violations
      ${_z7_macos_runtime_homebrew_violations})
  endif()

  if(_z7_macos_runtime_violations)
    string(REPLACE ";" "\n  " _z7_macos_runtime_violations_text
      "${_z7_macos_runtime_violations}")
    message(FATAL_ERROR
      "Banned LLVM C++ runtime load(s) in ${file_path}:\n"
      "  ${_z7_macos_runtime_violations_text}\n"
      "${_z7_macos_runtime_output}")
  endif()

  if(Z7_VALIDATE_REQUIRE_LLVM_RUNTIME)
    _z7_macos_runtime_require_expected_loads(
      "${file_path}"
      ${_z7_macos_runtime_load_paths})
  endif()
endfunction()

function(z7_macos_validate_bundled_llvm_runtime_dylib file_path expected_name)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR
      "Bundled Homebrew LLVM runtime is missing: ${file_path}")
  endif()

  _z7_macos_runtime_load_output(
    "${file_path}"
    _z7_macos_runtime_output)
  if(NOT _z7_macos_runtime_output_IS_MACHO)
    message(FATAL_ERROR
      "Bundled runtime is not a Mach-O dylib: ${file_path}\n"
      "${_z7_macos_runtime_output_ERROR}")
  endif()

  string(REPLACE "\n" ";" _z7_macos_runtime_lines
    "${_z7_macos_runtime_output}")
  list(LENGTH _z7_macos_runtime_lines _z7_macos_runtime_line_count)
  if(_z7_macos_runtime_line_count LESS 2)
    message(FATAL_ERROR
      "Cannot read bundled runtime install name: ${file_path}\n"
      "${_z7_macos_runtime_output}")
  endif()
  list(GET _z7_macos_runtime_lines 1 _z7_macos_runtime_id_line)
  string(STRIP "${_z7_macos_runtime_id_line}" _z7_macos_runtime_id_line)
  string(REGEX REPLACE " \\(.*$" "" _z7_macos_runtime_id
    "${_z7_macos_runtime_id_line}")
  if(NOT _z7_macos_runtime_id STREQUAL "@loader_path/${expected_name}" AND
     NOT _z7_macos_runtime_id STREQUAL "@rpath/${expected_name}")
    message(FATAL_ERROR
      "Bundled runtime install name must use @loader_path or @rpath: "
      "${file_path}\n"
      "Actual id: ${_z7_macos_runtime_id}\n"
      "${_z7_macos_runtime_output}")
  endif()

  z7_macos_validate_llvm_runtime_file(
    "${file_path}"
    FORBID_ABSOLUTE_HOMEBREW_RUNTIME
    LLVM_ROOT "${Z7_MACOS_DEPLOY_LLVM_ROOT}")

  foreach(_z7_macos_runtime_line IN LISTS _z7_macos_runtime_lines)
    string(STRIP "${_z7_macos_runtime_line}" _z7_macos_runtime_line)
    if(_z7_macos_runtime_line STREQUAL "" OR
       _z7_macos_runtime_line STREQUAL "${file_path}:")
      continue()
    endif()
    if(_z7_macos_runtime_line MATCHES "libc\\+\\+[^/ ]*\\.dylib|libunwind[^/ ]*\\.dylib")
      string(REGEX REPLACE " \\(.*$" "" _z7_macos_runtime_load
        "${_z7_macos_runtime_line}")
      if(NOT _z7_macos_runtime_load MATCHES "^(@loader_path|@rpath)/")
        message(FATAL_ERROR
          "Bundled runtime inter-dependency must use @loader_path or @rpath: "
          "${file_path}\n"
          "Bad load: ${_z7_macos_runtime_load}\n"
          "${_z7_macos_runtime_output}")
      endif()
    endif()
  endforeach()
endfunction()

function(z7_macos_validate_llvm_runtime_bundle contents_dir)
  if(NOT IS_DIRECTORY "${contents_dir}")
    message(FATAL_ERROR
      "Bundle Contents directory is missing: ${contents_dir}")
  endif()

  file(GLOB_RECURSE _z7_macos_runtime_bundle_files
    LIST_DIRECTORIES false
    "${contents_dir}/*")
  foreach(_z7_macos_runtime_bundle_file IN LISTS _z7_macos_runtime_bundle_files)
    z7_macos_validate_llvm_runtime_file(
      "${_z7_macos_runtime_bundle_file}"
      FORBID_ABSOLUTE_HOMEBREW_RUNTIME
      ALLOW_NON_MACHO
      LLVM_ROOT "${Z7_MACOS_DEPLOY_LLVM_ROOT}")
    _z7_macos_runtime_validate_bundle_load_targets(
      "${_z7_macos_runtime_bundle_file}"
      "${contents_dir}")
  endforeach()

  set(_z7_macos_runtime_frameworks "${contents_dir}/Frameworks")
  z7_macos_validate_bundled_llvm_runtime_dylib(
    "${_z7_macos_runtime_frameworks}/libc++.1.dylib"
    "libc++.1.dylib")
  z7_macos_validate_bundled_llvm_runtime_dylib(
    "${_z7_macos_runtime_frameworks}/libc++abi.1.dylib"
    "libc++abi.1.dylib")
  z7_macos_validate_bundled_llvm_runtime_dylib(
    "${_z7_macos_runtime_frameworks}/libunwind.1.dylib"
    "libunwind.1.dylib")

  message(STATUS
    "Verified release app bundle uses bundled Homebrew LLVM C++ runtimes")
endfunction()

if(Z7_MACOS_LLVM_RUNTIME_VALIDATE_RUNNER)
  if(NOT Z7_MACOS_VALIDATE_ENABLE)
    return()
  endif()
  if(NOT DEFINED Z7_MACOS_VALIDATE_FILE OR
     "${Z7_MACOS_VALIDATE_FILE}" STREQUAL "")
    message(FATAL_ERROR "Z7_MACOS_VALIDATE_FILE is required")
  endif()

  if(Z7_MACOS_VALIDATE_REQUIRE_LLVM_RUNTIME)
    set(_z7_macos_runtime_require_arg REQUIRE_LLVM_RUNTIME)
  else()
    set(_z7_macos_runtime_require_arg "")
  endif()

  z7_macos_validate_llvm_runtime_file(
    "${Z7_MACOS_VALIDATE_FILE}"
    ${_z7_macos_runtime_require_arg}
    LLVM_ROOT "${Z7_MACOS_VALIDATE_LLVM_ROOT}")
endif()
