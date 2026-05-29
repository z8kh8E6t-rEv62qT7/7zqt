if(Z7_MACOS_DEPLOY_RUNNER)
  include("${CMAKE_CURRENT_LIST_DIR}/Z7MacOSLlvmRuntimeValidation.cmake")

  function(_z7_macos_deploy_require variable_name)
    if(NOT DEFINED ${variable_name} OR "${${variable_name}}" STREQUAL "")
      message(FATAL_ERROR "${variable_name} is required")
    endif()
  endfunction()

  function(_z7_macos_deploy_print_captured label stdout stderr)
    if(NOT "${stdout}" STREQUAL "")
      message(STATUS "${label} stdout:\n${stdout}")
    endif()
    if(NOT "${stderr}" STREQUAL "")
      message(STATUS "${label} stderr:\n${stderr}")
    endif()
  endfunction()

  function(_z7_macos_deploy_run_process label)
    message(STATUS "${label}")
    execute_process(
      COMMAND ${ARGN}
      RESULT_VARIABLE _z7_macos_deploy_result
      OUTPUT_VARIABLE _z7_macos_deploy_stdout
      ERROR_VARIABLE _z7_macos_deploy_stderr)
    _z7_macos_deploy_print_captured(
      "${label}"
      "${_z7_macos_deploy_stdout}"
      "${_z7_macos_deploy_stderr}")
    if(NOT _z7_macos_deploy_result EQUAL 0)
      message(FATAL_ERROR
        "${label} failed with exit code ${_z7_macos_deploy_result}")
    endif()
    set(Z7_MACOS_DEPLOY_LAST_STDOUT
      "${_z7_macos_deploy_stdout}"
      PARENT_SCOPE)
    set(Z7_MACOS_DEPLOY_LAST_STDERR
      "${_z7_macos_deploy_stderr}"
      PARENT_SCOPE)
  endfunction()

  function(_z7_macos_deploy_find_required_program output_variable program_name)
    find_program(${output_variable}
      NAMES "${program_name}"
      NO_CACHE
      REQUIRED)
    set(${output_variable} "${${output_variable}}" PARENT_SCOPE)
  endfunction()

  function(_z7_macos_deploy_copy_llvm_runtime)
    _z7_macos_deploy_require(Z7_MACOS_DEPLOY_LLVM_ROOT)
    _z7_macos_deploy_require(Z7_MACOS_DEPLOY_APP_CONTENTS)

    set(_z7_macos_deploy_frameworks
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}/Frameworks")
    set(_z7_macos_deploy_libcxx
      "${Z7_MACOS_DEPLOY_LLVM_ROOT}/lib/c++/libc++.1.dylib")
    set(_z7_macos_deploy_libcxxabi
      "${Z7_MACOS_DEPLOY_LLVM_ROOT}/lib/c++/libc++abi.1.dylib")
    set(_z7_macos_deploy_libunwind
      "${Z7_MACOS_DEPLOY_LLVM_ROOT}/lib/unwind/libunwind.1.dylib")

    foreach(_z7_macos_deploy_runtime IN ITEMS
        "${_z7_macos_deploy_libcxx}"
        "${_z7_macos_deploy_libcxxabi}"
        "${_z7_macos_deploy_libunwind}")
      if(NOT EXISTS "${_z7_macos_deploy_runtime}")
        message(FATAL_ERROR
          "Required Homebrew LLVM runtime is missing: "
          "${_z7_macos_deploy_runtime}")
      endif()
    endforeach()

    file(MAKE_DIRECTORY "${_z7_macos_deploy_frameworks}")
    file(COPY_FILE
      "${_z7_macos_deploy_libcxx}"
      "${_z7_macos_deploy_frameworks}/libc++.1.dylib"
      ONLY_IF_DIFFERENT)
    file(COPY_FILE
      "${_z7_macos_deploy_libcxxabi}"
      "${_z7_macos_deploy_frameworks}/libc++abi.1.dylib"
      ONLY_IF_DIFFERENT)
    file(COPY_FILE
      "${_z7_macos_deploy_libunwind}"
      "${_z7_macos_deploy_frameworks}/libunwind.1.dylib"
      ONLY_IF_DIFFERENT)
  endfunction()

  function(_z7_macos_deploy_loader_path_to_frameworks output_variable binary_path)
    get_filename_component(_z7_macos_deploy_real_binary_path
      "${binary_path}"
      REALPATH)
    get_filename_component(_z7_macos_deploy_binary_dir
      "${_z7_macos_deploy_real_binary_path}"
      DIRECTORY)
    file(RELATIVE_PATH
      _z7_macos_deploy_frameworks_relative
      "${_z7_macos_deploy_binary_dir}"
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}/Frameworks")
    string(REGEX REPLACE "/+$" "" _z7_macos_deploy_frameworks_relative
      "${_z7_macos_deploy_frameworks_relative}")
    if(_z7_macos_deploy_frameworks_relative STREQUAL "" OR
       _z7_macos_deploy_frameworks_relative STREQUAL ".")
      set(${output_variable} "@loader_path" PARENT_SCOPE)
    else()
      set(${output_variable}
        "@loader_path/${_z7_macos_deploy_frameworks_relative}"
        PARENT_SCOPE)
    endif()
  endfunction()

  function(_z7_macos_deploy_append_runtime_change output_variable old_load new_load)
    if(old_load STREQUAL new_load)
      return()
    endif()

    set(_z7_macos_deploy_change_args "${${output_variable}}")
    list(FIND _z7_macos_deploy_change_args "${old_load}"
      _z7_macos_deploy_existing_change)
    if(_z7_macos_deploy_existing_change EQUAL -1)
      list(APPEND _z7_macos_deploy_change_args
        -change
        "${old_load}"
        "${new_load}")
    endif()
    set(${output_variable} "${_z7_macos_deploy_change_args}" PARENT_SCOPE)
  endfunction()

  function(_z7_macos_deploy_rewrite_runtime_loads binary_path otool_output)
    _z7_macos_deploy_loader_path_to_frameworks(
      _z7_macos_deploy_frameworks_loader_path
      "${binary_path}")

    set(_z7_macos_deploy_new_libcxx
      "${_z7_macos_deploy_frameworks_loader_path}/libc++.1.dylib")
    set(_z7_macos_deploy_new_libcxxabi
      "${_z7_macos_deploy_frameworks_loader_path}/libc++abi.1.dylib")
    set(_z7_macos_deploy_new_libunwind
      "${_z7_macos_deploy_frameworks_loader_path}/libunwind.1.dylib")

    set(_z7_macos_deploy_change_args "")
    string(REPLACE "\n" ";" _z7_macos_deploy_otool_lines
      "${otool_output}")
    foreach(_z7_macos_deploy_otool_line IN LISTS _z7_macos_deploy_otool_lines)
      string(STRIP
        "${_z7_macos_deploy_otool_line}"
        _z7_macos_deploy_otool_line)
      if(_z7_macos_deploy_otool_line STREQUAL "" OR
         _z7_macos_deploy_otool_line MATCHES ":$")
        continue()
      endif()

      string(REGEX REPLACE
        " \\(.*$"
        ""
        _z7_macos_deploy_old_load
        "${_z7_macos_deploy_otool_line}")
      get_filename_component(
        _z7_macos_deploy_old_load_name
        "${_z7_macos_deploy_old_load}"
        NAME)
      if(_z7_macos_deploy_old_load_name STREQUAL "libc++.1.dylib")
        _z7_macos_deploy_append_runtime_change(
          _z7_macos_deploy_change_args
          "${_z7_macos_deploy_old_load}"
          "${_z7_macos_deploy_new_libcxx}")
      elseif(_z7_macos_deploy_old_load_name STREQUAL "libc++abi.1.dylib" OR
             _z7_macos_deploy_old_load_name STREQUAL "libc++abi.dylib")
        _z7_macos_deploy_append_runtime_change(
          _z7_macos_deploy_change_args
          "${_z7_macos_deploy_old_load}"
          "${_z7_macos_deploy_new_libcxxabi}")
      elseif(_z7_macos_deploy_old_load_name STREQUAL "libunwind.1.dylib" OR
             _z7_macos_deploy_old_load_name STREQUAL "libunwind.dylib")
        _z7_macos_deploy_append_runtime_change(
          _z7_macos_deploy_change_args
          "${_z7_macos_deploy_old_load}"
          "${_z7_macos_deploy_new_libunwind}")
      endif()
    endforeach()

    if(_z7_macos_deploy_change_args)
      _z7_macos_deploy_run_process(
        "rewriting LLVM runtime loads in ${binary_path}"
        "${Z7_INSTALL_NAME_TOOL_EXECUTABLE}"
        ${_z7_macos_deploy_change_args}
        "${binary_path}")
    endif()
  endfunction()

  function(_z7_macos_deploy_rewrite_llvm_runtime)
    _z7_macos_deploy_find_required_program(
      Z7_INSTALL_NAME_TOOL_EXECUTABLE
      install_name_tool)
    _z7_macos_deploy_find_required_program(
      Z7_OTOOL_EXECUTABLE
      otool)

    set(_z7_macos_deploy_frameworks
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}/Frameworks")
    set(_z7_macos_deploy_libcxx
      "${_z7_macos_deploy_frameworks}/libc++.1.dylib")
    set(_z7_macos_deploy_libcxxabi
      "${_z7_macos_deploy_frameworks}/libc++abi.1.dylib")
    set(_z7_macos_deploy_libunwind
      "${_z7_macos_deploy_frameworks}/libunwind.1.dylib")

    _z7_macos_deploy_run_process(
      "setting bundled libc++ install names"
      "${Z7_INSTALL_NAME_TOOL_EXECUTABLE}"
      -id
      "@loader_path/libc++.1.dylib"
      "${_z7_macos_deploy_libcxx}")
    _z7_macos_deploy_run_process(
      "setting bundled libc++abi install names"
      "${Z7_INSTALL_NAME_TOOL_EXECUTABLE}"
      -id
      "@loader_path/libc++abi.1.dylib"
      "${_z7_macos_deploy_libcxxabi}")
    _z7_macos_deploy_run_process(
      "setting bundled libunwind install names"
      "${Z7_INSTALL_NAME_TOOL_EXECUTABLE}"
      -id
      "@loader_path/libunwind.1.dylib"
      "${_z7_macos_deploy_libunwind}")

    file(GLOB_RECURSE _z7_macos_deploy_bundle_files
      LIST_DIRECTORIES false
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}/*")
    foreach(_z7_macos_deploy_bundle_file IN LISTS _z7_macos_deploy_bundle_files)
      execute_process(
        COMMAND
          "${Z7_OTOOL_EXECUTABLE}"
          -L
          "${_z7_macos_deploy_bundle_file}"
        RESULT_VARIABLE _z7_macos_deploy_otool_result
        OUTPUT_VARIABLE _z7_macos_deploy_otool_output
        ERROR_QUIET)
      if(_z7_macos_deploy_otool_result EQUAL 0)
        _z7_macos_deploy_rewrite_runtime_loads(
          "${_z7_macos_deploy_bundle_file}"
          "${_z7_macos_deploy_otool_output}")
      endif()
    endforeach()
  endfunction()

  function(_z7_macos_deploy_verify_llvm_runtime)
    _z7_macos_deploy_find_required_program(
      Z7_OTOOL_EXECUTABLE
      otool)
    z7_macos_validate_llvm_runtime_bundle(
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}")
  endfunction()

  _z7_macos_deploy_require(Z7_MACDEPLOYQT_EXECUTABLE)
  _z7_macos_deploy_require(Z7_CODESIGN_EXECUTABLE)
  _z7_macos_deploy_require(Z7_MACOS_DEPLOY_APP)
  _z7_macos_deploy_require(Z7_MACOS_DEPLOY_APP_CONTENTS)
  _z7_macos_deploy_require(Z7_MACOS_DEPLOY_HELPER_EXECUTABLE)
  _z7_macos_deploy_require(Z7_MACOS_DEPLOY_LIBPATHS)
  _z7_macos_deploy_require(Z7_MACOS_DEPLOY_ENFORCE_LLVM_RUNTIME)

  string(REPLACE "|" ";" _z7_macos_deploy_libpaths
    "${Z7_MACOS_DEPLOY_LIBPATHS}")

  set(_z7_macos_deploy_macdeployqt_args
    "${Z7_MACOS_DEPLOY_APP}"
    "-executable=${Z7_MACOS_DEPLOY_HELPER_EXECUTABLE}")
  foreach(_z7_macos_deploy_libpath IN LISTS _z7_macos_deploy_libpaths)
    list(APPEND _z7_macos_deploy_macdeployqt_args
      "-libpath=${_z7_macos_deploy_libpath}")
  endforeach()
  list(APPEND _z7_macos_deploy_macdeployqt_args
    -always-overwrite
    -codesign=-)

  # Homebrew's split Qt modules can leave plugin-level framework references
  # unresolved until macdeployqt can see the copied plugin tree.
  _z7_macos_deploy_run_process(
    "macdeployqt first pass"
    "${Z7_MACDEPLOYQT_EXECUTABLE}"
    ${_z7_macos_deploy_macdeployqt_args})
  _z7_macos_deploy_run_process(
    "macdeployqt second pass"
    "${Z7_MACDEPLOYQT_EXECUTABLE}"
    ${_z7_macos_deploy_macdeployqt_args})

  set(_z7_macos_deploy_second_output
    "${Z7_MACOS_DEPLOY_LAST_STDOUT}\n${Z7_MACOS_DEPLOY_LAST_STDERR}")
  if(_z7_macos_deploy_second_output MATCHES "(^|\n)ERROR:")
    message(FATAL_ERROR "macdeployqt second pass reported unresolved errors")
  endif()

  if(Z7_MACOS_DEPLOY_ENFORCE_LLVM_RUNTIME)
    _z7_macos_deploy_copy_llvm_runtime()
    _z7_macos_deploy_rewrite_llvm_runtime()
    _z7_macos_deploy_verify_llvm_runtime()
  endif()

  _z7_macos_deploy_run_process(
    "codesign"
    "${Z7_CODESIGN_EXECUTABLE}"
    --force
    --sign
    -
    --deep
    "${Z7_MACOS_DEPLOY_APP}")
  _z7_macos_deploy_run_process(
    "codesign verification"
    "${Z7_CODESIGN_EXECUTABLE}"
    --verify
    --deep
    --strict
    "${Z7_MACOS_DEPLOY_APP}")
  return()
endif()

include_guard(GLOBAL)

if(NOT APPLE)
  return()
endif()

set(_z7_macos_deploy_qt_bin_hints "")
set(_z7_macos_deploy_qt_lib_hints "")

if(TARGET Qt6::qmake)
  get_target_property(_z7_macos_deploy_qmake_path Qt6::qmake IMPORTED_LOCATION)
  if(_z7_macos_deploy_qmake_path)
    get_filename_component(
      _z7_macos_deploy_qt_bin_dir
      "${_z7_macos_deploy_qmake_path}"
      DIRECTORY)
    list(APPEND _z7_macos_deploy_qt_bin_hints
      "${_z7_macos_deploy_qt_bin_dir}")
    get_filename_component(
      _z7_macos_deploy_qt_prefix_from_qmake
      "${_z7_macos_deploy_qt_bin_dir}/.."
      ABSOLUTE)
    list(APPEND _z7_macos_deploy_qt_lib_hints
      "${_z7_macos_deploy_qt_prefix_from_qmake}/lib")
  endif()
endif()

if(Z7_QT_ROOT)
  list(APPEND _z7_macos_deploy_qt_bin_hints "${Z7_QT_ROOT}/bin")
  list(APPEND _z7_macos_deploy_qt_lib_hints "${Z7_QT_ROOT}/lib")
endif()

if(Qt6_DIR)
  get_filename_component(
    _z7_macos_deploy_qt_prefix
    "${Qt6_DIR}/../../.."
    ABSOLUTE)
  list(APPEND _z7_macos_deploy_qt_bin_hints
    "${_z7_macos_deploy_qt_prefix}/bin")
  list(APPEND _z7_macos_deploy_qt_lib_hints
    "${_z7_macos_deploy_qt_prefix}/lib")
endif()

list(APPEND _z7_macos_deploy_qt_lib_hints
  "/opt/homebrew/lib"
  "/usr/local/lib")
list(REMOVE_DUPLICATES _z7_macos_deploy_qt_lib_hints)

set(_z7_macos_deploy_macdeployqt_libpaths
  "$<TARGET_FILE_DIR:z7_shared_runtime>"
  "$<TARGET_FILE_DIR:z7_third_party>")
foreach(_z7_macos_deploy_qt_lib_hint IN LISTS _z7_macos_deploy_qt_lib_hints)
  if(EXISTS "${_z7_macos_deploy_qt_lib_hint}")
    list(APPEND _z7_macos_deploy_macdeployqt_libpaths
      "${_z7_macos_deploy_qt_lib_hint}")
  endif()
endforeach()
string(JOIN "|" _z7_macos_deploy_macdeployqt_libpaths_arg
  ${_z7_macos_deploy_macdeployqt_libpaths})

find_program(Z7_MACDEPLOYQT_EXECUTABLE
  NAMES macdeployqt
  HINTS
    ${_z7_macos_deploy_qt_bin_hints}
  PATHS
    "/opt/homebrew/bin"
    "/usr/local/bin"
  NO_DEFAULT_PATH
  DOC "Path to Qt macdeployqt used by the deploy_macos target.")

if(NOT Z7_MACDEPLOYQT_EXECUTABLE)
  find_program(Z7_MACDEPLOYQT_EXECUTABLE
    NAMES macdeployqt
    DOC "Path to Qt macdeployqt used by the deploy_macos target.")
endif()

if(NOT Z7_MACDEPLOYQT_EXECUTABLE)
  message(FATAL_ERROR
    "macdeployqt was not found. Install Qt for macOS or set "
    "Z7_MACDEPLOYQT_EXECUTABLE to the macdeployqt executable.")
endif()

find_program(Z7_CODESIGN_EXECUTABLE
  NAMES codesign
  REQUIRED
  DOC "Path to codesign used by the deploy_macos target.")

set(Z7_MACOS_DEPLOY_ROOT "${PROJECT_BINARY_DIR}/deploy")
set(Z7_MACOS_DEPLOY_APP "${Z7_MACOS_DEPLOY_ROOT}/7zFM.app")
set(Z7_MACOS_DEPLOY_APP_CONTENTS "${Z7_MACOS_DEPLOY_APP}/Contents")
set(Z7_MACOS_DEPLOY_APP_MACOS "${Z7_MACOS_DEPLOY_APP_CONTENTS}/MacOS")
set(Z7_MACOS_DEPLOY_GENERATED_INFO_PLIST
  "${Z7_MACOS_DEPLOY_ROOT}/generated/7zFM.Info.plist")

set(MACOSX_BUNDLE_EXECUTABLE_NAME "7zFM")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "app.sevenzip")
set(MACOSX_BUNDLE_BUNDLE_NAME "7zFM")
set(MACOSX_BUNDLE_BUNDLE_VERSION "1")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
set(MACOSX_BUNDLE_LONG_VERSION_STRING "7-Zip File Manager 1.0")
configure_file(
  "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
  "${Z7_MACOS_DEPLOY_GENERATED_INFO_PLIST}")

add_custom_target(deploy_macos
  COMMAND
    "${CMAKE_COMMAND}" -E rm -rf "${Z7_MACOS_DEPLOY_APP}"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory "${Z7_MACOS_DEPLOY_APP_MACOS}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
      "${Z7_MACOS_DEPLOY_GENERATED_INFO_PLIST}"
      "${Z7_MACOS_DEPLOY_APP_CONTENTS}/Info.plist"
  COMMAND
    "${CMAKE_COMMAND}" -E copy
      "$<TARGET_FILE:7zFM>"
      "${Z7_MACOS_DEPLOY_APP_MACOS}/7zFM"
  COMMAND
    "${CMAKE_COMMAND}" -E copy
      "$<TARGET_FILE:7zG>"
      "${Z7_MACOS_DEPLOY_APP_MACOS}/7zG"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
      "${Z7_EXECUTABLE_OUTPUT_DIR}/7z.sfx"
      "${Z7_MACOS_DEPLOY_APP_MACOS}/7z.sfx"
  COMMAND
    "${CMAKE_COMMAND}"
      -DZ7_MACOS_DEPLOY_RUNNER=ON
      "-DZ7_MACDEPLOYQT_EXECUTABLE=${Z7_MACDEPLOYQT_EXECUTABLE}"
      "-DZ7_CODESIGN_EXECUTABLE=${Z7_CODESIGN_EXECUTABLE}"
      "-DZ7_MACOS_DEPLOY_APP=${Z7_MACOS_DEPLOY_APP}"
      "-DZ7_MACOS_DEPLOY_APP_CONTENTS=${Z7_MACOS_DEPLOY_APP_CONTENTS}"
      "-DZ7_MACOS_DEPLOY_HELPER_EXECUTABLE=${Z7_MACOS_DEPLOY_APP_MACOS}/7zG"
      "-DZ7_MACOS_DEPLOY_LIBPATHS=${_z7_macos_deploy_macdeployqt_libpaths_arg}"
      "-DZ7_MACOS_DEPLOY_ENFORCE_LLVM_RUNTIME=$<CONFIG:Release>"
      "-DZ7_MACOS_DEPLOY_LLVM_ROOT=${Z7_MACOS_LLVM_ROOT}"
      -P "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Deploying ${Z7_MACOS_DEPLOY_APP}"
  VERBATIM)

add_dependencies(deploy_macos
  7zFM
  7zG
  z7_sfx_module_7z
  z7_shared_runtime
  z7_third_party)
