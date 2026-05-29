# VerifyNativeBridgeUsage.cmake
# Ensures third_party_adapter/third_party_adapter.h is only included from the
# current native archive backend implementation tree.

if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(_z7_root "${PROJECT_SOURCE_DIR}")

set(_z7_scan_globs
  "src/*.h"
  "src/*.hpp"
  "src/*.hh"
  "src/*.c"
  "src/*.cpp"
  "src/*.cxx"
  "tests/*.h"
  "tests/*.hpp"
  "tests/*.hh"
  "tests/*.c"
  "tests/*.cpp"
  "tests/*.cxx")

set(_z7_sources)
foreach(_z7_glob IN LISTS _z7_scan_globs)
  file(GLOB_RECURSE _z7_found RELATIVE "${_z7_root}" "${_z7_glob}")
  list(APPEND _z7_sources ${_z7_found})
endforeach()
list(REMOVE_DUPLICATES _z7_sources)

set(_z7_violations)

foreach(_z7_rel IN LISTS _z7_sources)
  if(_z7_rel MATCHES "^src/third_party/")
    continue()
  endif()

  set(_z7_abs "${_z7_root}/${_z7_rel}")
  if(NOT EXISTS "${_z7_abs}")
    continue()
  endif()

  file(READ "${_z7_abs}" _z7_content)
  string(REGEX MATCHALL "#[ \t]*include[ \t]*\"([^\"]+)\"" _z7_include_lines "${_z7_content}")

  foreach(_z7_line IN LISTS _z7_include_lines)
    string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" _z7_inc "${_z7_line}")
    string(FIND "${_z7_inc}" "third_party_adapter/third_party_adapter.h" _z7_bridge_pos)
    if(_z7_bridge_pos GREATER -1)
      if(NOT _z7_rel MATCHES "^src/archive_application/src/native_7z/")
        list(APPEND _z7_violations "${_z7_rel}: #include \"${_z7_inc}\"")
      endif()
    endif()
  endforeach()
endforeach()

if(_z7_violations)
  list(JOIN _z7_violations "\n  " _z7_violation_text)
  message(FATAL_ERROR
    "third_party_adapter/third_party_adapter.h may only be included from src/archive_application/src/native_7z/ sources:\n"
    "  ${_z7_violation_text}")
endif()

message(STATUS "Native bridge usage boundary check passed.")
