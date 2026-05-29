include_guard(GLOBAL)

set(Z7_LLVM_COVERAGE_PROFILE_DIR "${PROJECT_BINARY_DIR}/profiles")
set(Z7_LLVM_COVERAGE_REPORT_DIR "${PROJECT_BINARY_DIR}/coverage")
set(Z7_LLVM_COVERAGE_PROFDATA
    "${Z7_LLVM_COVERAGE_REPORT_DIR}/coverage.profdata")
set(Z7_LLVM_COVERAGE_SUMMARY
    "${Z7_LLVM_COVERAGE_REPORT_DIR}/summary.txt")
set(Z7_LLVM_COVERAGE_HTML_DIR
    "${Z7_LLVM_COVERAGE_REPORT_DIR}/html")
set(Z7_LLVM_COVERAGE_SOURCES_FILE
    "${Z7_LLVM_COVERAGE_REPORT_DIR}/sources.txt")

function(z7_configure_llvm_coverage)
  if(NOT Z7_ENABLE_LLVM_COVERAGE)
    return()
  endif()

  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
      "Z7_ENABLE_LLVM_COVERAGE requires Clang or AppleClang. "
      "Current C compiler is ${CMAKE_C_COMPILER_ID}.")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
      "Z7_ENABLE_LLVM_COVERAGE requires Clang or AppleClang. "
      "Current C++ compiler is ${CMAKE_CXX_COMPILER_ID}.")
  endif()

  set(_z7_llvm_tool_hints)
  if(DEFINED Z7_PREFERRED_HOMEBREW_LLVM_ROOT)
    list(APPEND _z7_llvm_tool_hints "${Z7_PREFERRED_HOMEBREW_LLVM_ROOT}/bin")
  endif()
  if(APPLE)
    list(APPEND _z7_llvm_tool_hints
      "/opt/homebrew/opt/llvm/bin"
      "/usr/local/opt/llvm/bin")
  endif()

  if(NOT Z7_LLVM_COV_EXECUTABLE)
    find_program(Z7_LLVM_COV_EXECUTABLE
      NAMES llvm-cov
      PATHS ${_z7_llvm_tool_hints}
      NO_DEFAULT_PATH
      DOC "Path to llvm-cov used by the z7 coverage-report target.")
  endif()
  find_program(Z7_LLVM_COV_EXECUTABLE
    NAMES llvm-cov
    DOC "Path to llvm-cov used by the z7 coverage-report target.")

  if(NOT Z7_LLVM_PROFDATA_EXECUTABLE)
    find_program(Z7_LLVM_PROFDATA_EXECUTABLE
      NAMES llvm-profdata
      PATHS ${_z7_llvm_tool_hints}
      NO_DEFAULT_PATH
      DOC "Path to llvm-profdata used by the z7 coverage-report target.")
  endif()
  find_program(Z7_LLVM_PROFDATA_EXECUTABLE
    NAMES llvm-profdata
    DOC "Path to llvm-profdata used by the z7 coverage-report target.")

  if(NOT Z7_LLVM_COV_EXECUTABLE)
    message(FATAL_ERROR "Z7_ENABLE_LLVM_COVERAGE requires llvm-cov.")
  endif()
  if(NOT Z7_LLVM_PROFDATA_EXECUTABLE)
    message(FATAL_ERROR "Z7_ENABLE_LLVM_COVERAGE requires llvm-profdata.")
  endif()

  get_filename_component(_z7_coverage_script
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Z7LlvmCoverageReport.cmake"
    ABSOLUTE)
  set(Z7_LLVM_COVERAGE_REPORT_SCRIPT "${_z7_coverage_script}" CACHE INTERNAL
      "CMake script that generates LLVM coverage reports.")

  message(STATUS "z7qt: LLVM coverage enabled")
  message(STATUS "z7qt: llvm-cov = ${Z7_LLVM_COV_EXECUTABLE}")
  message(STATUS "z7qt: llvm-profdata = ${Z7_LLVM_PROFDATA_EXECUTABLE}")
endfunction()

function(z7_apply_llvm_coverage_to_target target_name)
  if(NOT Z7_ENABLE_LLVM_COVERAGE)
    return()
  endif()

  if(NOT TARGET "${target_name}")
    return()
  endif()

  target_compile_options("${target_name}" PRIVATE
    "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-fprofile-instr-generate>"
    "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-fcoverage-mapping>")

  get_target_property(_z7_target_type "${target_name}" TYPE)
  if(_z7_target_type STREQUAL "EXECUTABLE" OR
     _z7_target_type STREQUAL "SHARED_LIBRARY" OR
     _z7_target_type STREQUAL "MODULE_LIBRARY")
    target_link_options("${target_name}" PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping)
    set_property(GLOBAL APPEND PROPERTY
      Z7_LLVM_COVERAGE_OBJECT_TARGETS "${target_name}")
  endif()
endfunction()

function(z7_add_llvm_coverage_targets)
  if(NOT Z7_ENABLE_LLVM_COVERAGE)
    return()
  endif()

  get_property(_z7_coverage_targets GLOBAL PROPERTY
    Z7_LLVM_COVERAGE_OBJECT_TARGETS)
  if(NOT _z7_coverage_targets)
    message(FATAL_ERROR
      "Z7_ENABLE_LLVM_COVERAGE is ON but no executable/shared/module targets "
      "were registered for reporting.")
  endif()
  list(REMOVE_DUPLICATES _z7_coverage_targets)

  set(_z7_coverage_objects)
  foreach(_z7_coverage_target IN LISTS _z7_coverage_targets)
    if(TARGET "${_z7_coverage_target}")
      list(APPEND _z7_coverage_objects "$<TARGET_FILE:${_z7_coverage_target}>")
    endif()
  endforeach()

  add_custom_target(coverage-reset
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${Z7_LLVM_COVERAGE_PROFILE_DIR}"
            "${Z7_LLVM_COVERAGE_REPORT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${Z7_LLVM_COVERAGE_PROFILE_DIR}"
            "${Z7_LLVM_COVERAGE_REPORT_DIR}"
    COMMENT "Resetting LLVM coverage profiles and reports"
    VERBATIM)

  add_custom_target(coverage-report
    COMMAND "${CMAKE_COMMAND}"
            "-DZ7_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DZ7_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DZ7_LLVM_COV_EXECUTABLE=${Z7_LLVM_COV_EXECUTABLE}"
            "-DZ7_LLVM_PROFDATA_EXECUTABLE=${Z7_LLVM_PROFDATA_EXECUTABLE}"
            "-DZ7_PROFILE_DIR=${Z7_LLVM_COVERAGE_PROFILE_DIR}"
            "-DZ7_REPORT_DIR=${Z7_LLVM_COVERAGE_REPORT_DIR}"
            "-DZ7_PROFDATA_FILE=${Z7_LLVM_COVERAGE_PROFDATA}"
            "-DZ7_SUMMARY_FILE=${Z7_LLVM_COVERAGE_SUMMARY}"
            "-DZ7_HTML_DIR=${Z7_LLVM_COVERAGE_HTML_DIR}"
            "-DZ7_SOURCES_FILE=${Z7_LLVM_COVERAGE_SOURCES_FILE}"
            "-DZ7_COVERAGE_OBJECTS=$<JOIN:${_z7_coverage_objects},|>"
            -P "${Z7_LLVM_COVERAGE_REPORT_SCRIPT}"
    DEPENDS ${_z7_coverage_targets}
    COMMENT "Generating LLVM coverage report"
    VERBATIM
    COMMAND_EXPAND_LISTS)
endfunction()
