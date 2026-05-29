include_guard(GLOBAL)

set(_z7_unused_script_dir "${CMAKE_CURRENT_LIST_DIR}/unused")
set(_z7_unused_output_dir "${CMAKE_BINARY_DIR}/analysis/unused")

if(NOT EXISTS "${_z7_unused_script_dir}/run_tidy.cmake")
  message(FATAL_ERROR "z7_unused: missing script ${_z7_unused_script_dir}/run_tidy.cmake")
endif()
if(NOT EXISTS "${_z7_unused_script_dir}/run_warnings.cmake")
  message(FATAL_ERROR "z7_unused: missing script ${_z7_unused_script_dir}/run_warnings.cmake")
endif()
if(NOT EXISTS "${_z7_unused_script_dir}/run_functions.cmake")
  message(FATAL_ERROR "z7_unused: missing script ${_z7_unused_script_dir}/run_functions.cmake")
endif()
if(NOT EXISTS "${_z7_unused_script_dir}/generate_report.cmake")
  message(FATAL_ERROR "z7_unused: missing script ${_z7_unused_script_dir}/generate_report.cmake")
endif()

set(_z7_unused_common_args
  -DSOURCE_DIR:PATH=${CMAKE_SOURCE_DIR}
  -DBINARY_DIR:PATH=${CMAKE_BINARY_DIR}
  -DOUTPUT_DIR:PATH=${_z7_unused_output_dir})

add_custom_target(z7_unused_tidy
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_z7_unused_output_dir}"
  COMMAND "${CMAKE_COMMAND}" ${_z7_unused_common_args}
          -P "${_z7_unused_script_dir}/run_tidy.cmake"
  COMMENT "z7 unused analysis: running clang-tidy"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_unused_warnings
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_z7_unused_output_dir}"
  COMMAND "${CMAKE_COMMAND}" ${_z7_unused_common_args}
          -P "${_z7_unused_script_dir}/run_warnings.cmake"
  COMMENT "z7 unused analysis: running clang-check warning scan"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_unused_functions
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_z7_unused_output_dir}"
  COMMAND "${CMAKE_COMMAND}" ${_z7_unused_common_args}
          -P "${_z7_unused_script_dir}/run_functions.cmake"
  COMMENT "z7 unused analysis: extracting function candidates"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_unused_report
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_z7_unused_output_dir}"
  COMMAND "${CMAKE_COMMAND}" ${_z7_unused_common_args}
          -P "${_z7_unused_script_dir}/generate_report.cmake"
  DEPENDS z7_unused_tidy z7_unused_warnings z7_unused_functions
  COMMENT "z7 unused analysis: generating merged report"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_unused_analysis
  DEPENDS z7_unused_report)
