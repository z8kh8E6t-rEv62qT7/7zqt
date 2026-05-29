if(NOT DEFINED Z7_SOURCE_DIR OR Z7_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "Z7_SOURCE_DIR is required.")
endif()
if(NOT DEFINED Z7_BINARY_DIR OR Z7_BINARY_DIR STREQUAL "")
  message(FATAL_ERROR "Z7_BINARY_DIR is required.")
endif()
if(NOT DEFINED Z7_LLVM_COV_EXECUTABLE OR
   Z7_LLVM_COV_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "Z7_LLVM_COV_EXECUTABLE is required.")
endif()
if(NOT DEFINED Z7_LLVM_PROFDATA_EXECUTABLE OR
   Z7_LLVM_PROFDATA_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "Z7_LLVM_PROFDATA_EXECUTABLE is required.")
endif()
if(NOT DEFINED Z7_PROFILE_DIR OR Z7_PROFILE_DIR STREQUAL "")
  message(FATAL_ERROR "Z7_PROFILE_DIR is required.")
endif()
if(NOT DEFINED Z7_REPORT_DIR OR Z7_REPORT_DIR STREQUAL "")
  message(FATAL_ERROR "Z7_REPORT_DIR is required.")
endif()
if(NOT DEFINED Z7_PROFDATA_FILE OR Z7_PROFDATA_FILE STREQUAL "")
  message(FATAL_ERROR "Z7_PROFDATA_FILE is required.")
endif()
if(NOT DEFINED Z7_SUMMARY_FILE OR Z7_SUMMARY_FILE STREQUAL "")
  message(FATAL_ERROR "Z7_SUMMARY_FILE is required.")
endif()
if(NOT DEFINED Z7_HTML_DIR OR Z7_HTML_DIR STREQUAL "")
  message(FATAL_ERROR "Z7_HTML_DIR is required.")
endif()
if(NOT DEFINED Z7_SOURCES_FILE OR Z7_SOURCES_FILE STREQUAL "")
  message(FATAL_ERROR "Z7_SOURCES_FILE is required.")
endif()
if(NOT DEFINED Z7_COVERAGE_OBJECTS OR Z7_COVERAGE_OBJECTS STREQUAL "")
  message(FATAL_ERROR "Z7_COVERAGE_OBJECTS is required.")
endif()

file(GLOB _z7_profraw_files
  LIST_DIRECTORIES FALSE
  "${Z7_PROFILE_DIR}/*.profraw")
if(NOT _z7_profraw_files)
  message(FATAL_ERROR
    "No .profraw files found in ${Z7_PROFILE_DIR}. "
    "Run ctest with LLVM_PROFILE_FILE pointing at that directory first.")
endif()

file(MAKE_DIRECTORY "${Z7_REPORT_DIR}")
file(REMOVE_RECURSE "${Z7_HTML_DIR}")
file(MAKE_DIRECTORY "${Z7_HTML_DIR}")

execute_process(
  COMMAND "${Z7_LLVM_PROFDATA_EXECUTABLE}" merge -sparse
          ${_z7_profraw_files}
          -o "${Z7_PROFDATA_FILE}"
  RESULT_VARIABLE _z7_profdata_rc)
if(NOT _z7_profdata_rc EQUAL 0)
  message(FATAL_ERROR "llvm-profdata merge failed with exit code ${_z7_profdata_rc}.")
endif()

file(GLOB_RECURSE _z7_coverage_sources
  LIST_DIRECTORIES FALSE
  "${Z7_SOURCE_DIR}/src/*.c"
  "${Z7_SOURCE_DIR}/src/*.cc"
  "${Z7_SOURCE_DIR}/src/*.cpp"
  "${Z7_SOURCE_DIR}/src/*.cxx"
  "${Z7_SOURCE_DIR}/src/*.m"
  "${Z7_SOURCE_DIR}/src/*.mm"
  "${Z7_SOURCE_DIR}/src/*.h"
  "${Z7_SOURCE_DIR}/src/*.hh"
  "${Z7_SOURCE_DIR}/src/*.hpp"
  "${Z7_SOURCE_DIR}/src/*.hxx")

set(_z7_filtered_sources)
foreach(_z7_source IN LISTS _z7_coverage_sources)
  file(RELATIVE_PATH _z7_relative_source "${Z7_SOURCE_DIR}" "${_z7_source}")
  if(_z7_relative_source MATCHES "^src/macos_integration/xcode_project/")
    continue()
  endif()
  if(_z7_relative_source MATCHES "^src/third_party/original_7zip/")
    continue()
  endif()
  list(APPEND _z7_filtered_sources "${_z7_source}")
endforeach()

if(NOT _z7_filtered_sources)
  message(FATAL_ERROR "No production sources found for LLVM coverage reporting.")
endif()
list(SORT _z7_filtered_sources)

string(REPLACE ";" "\n" _z7_sources_text "${_z7_filtered_sources}")
file(WRITE "${Z7_SOURCES_FILE}" "${_z7_sources_text}\n")

string(REPLACE "|" ";" _z7_coverage_objects "${Z7_COVERAGE_OBJECTS}")
set(_z7_existing_objects)
foreach(_z7_object IN LISTS _z7_coverage_objects)
  if(EXISTS "${_z7_object}")
    list(APPEND _z7_existing_objects "${_z7_object}")
  endif()
endforeach()
if(NOT _z7_existing_objects)
  message(FATAL_ERROR "No coverage object files exist.")
endif()

list(POP_FRONT _z7_existing_objects _z7_main_object)
set(_z7_object_args)
foreach(_z7_object IN LISTS _z7_existing_objects)
  list(APPEND _z7_object_args "--object=${_z7_object}")
endforeach()

set(_z7_ignore_regex
  "(^|/)(tests|build)/|(.*/)?src/(macos_integration/xcode_project|third_party/original_7zip)(/|$)")

execute_process(
  COMMAND "${Z7_LLVM_COV_EXECUTABLE}" report
          "-instr-profile=${Z7_PROFDATA_FILE}"
          "-ignore-filename-regex=${_z7_ignore_regex}"
          -show-region-summary
          -show-branch-summary
          "${_z7_main_object}"
          ${_z7_object_args}
          -sources ${_z7_filtered_sources}
  RESULT_VARIABLE _z7_report_rc
  OUTPUT_VARIABLE _z7_report_stdout
  ERROR_VARIABLE _z7_report_stderr)
if(NOT _z7_report_rc EQUAL 0)
  message("${_z7_report_stdout}")
  message("${_z7_report_stderr}")
  message(FATAL_ERROR "llvm-cov report failed with exit code ${_z7_report_rc}.")
endif()
file(WRITE "${Z7_SUMMARY_FILE}" "${_z7_report_stdout}")

execute_process(
  COMMAND "${Z7_LLVM_COV_EXECUTABLE}" show
          "-instr-profile=${Z7_PROFDATA_FILE}"
          "-ignore-filename-regex=${_z7_ignore_regex}"
          -show-line-counts-or-regions
          -format=html
          "-output-dir=${Z7_HTML_DIR}"
          "${_z7_main_object}"
          ${_z7_object_args}
          -sources ${_z7_filtered_sources}
  RESULT_VARIABLE _z7_show_rc
  OUTPUT_VARIABLE _z7_show_stdout
  ERROR_VARIABLE _z7_show_stderr)
if(NOT _z7_show_rc EQUAL 0)
  message("${_z7_show_stdout}")
  message("${_z7_show_stderr}")
  message(FATAL_ERROR "llvm-cov show failed with exit code ${_z7_show_rc}.")
endif()

message(STATUS "LLVM coverage summary: ${Z7_SUMMARY_FILE}")
message(STATUS "LLVM coverage HTML: ${Z7_HTML_DIR}/index.html")
