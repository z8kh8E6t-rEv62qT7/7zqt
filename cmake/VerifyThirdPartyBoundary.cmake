# VerifyThirdPartyBoundary.cmake
# Fails when non-vendored source files directly include original 7-Zip headers
# outside the current native bridge header.

if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(_z7_root "${PROJECT_SOURCE_DIR}")

set(_z7_allowed_bridge_files
  "src/archive_application/src/native_7z/third_party_adapter/third_party_adapter.h")

set(_z7_forbidden_include_regex
  "^(Common/|7zip/|Windows/|Archive/|C/|CpuArch\\.h$|Precomp\\.h$|MyVersion\\.h$)")

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
  "tests/*.cxx"
  "include/*.h"
  "include/*.hpp"
  "include/*.hh")

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
    if(_z7_inc MATCHES "${_z7_forbidden_include_regex}")
      list(FIND _z7_allowed_bridge_files "${_z7_rel}" _z7_allowed_index)
      if(_z7_allowed_index EQUAL -1)
        list(APPEND _z7_violations "${_z7_rel}: #include \"${_z7_inc}\"")
      endif()
    endif()
  endforeach()
endforeach()

if(_z7_violations)
  list(JOIN _z7_violations "\n  " _z7_violation_text)
  message(FATAL_ERROR
    "Forbidden upstream includes found outside allowed bridge headers:\n"
    "  ${_z7_violation_text}\n"
    "Allowed adapter include boundaries:\n"
    "  src/archive_application/src/native_7z/third_party_adapter/third_party_adapter.h")
endif()

message(STATUS "Third-party include boundary check passed.")
