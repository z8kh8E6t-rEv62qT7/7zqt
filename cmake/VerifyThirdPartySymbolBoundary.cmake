# VerifyThirdPartySymbolBoundary.cmake
# Fails when public headers expose original 7-Zip private type names.

if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(_z7_root "${PROJECT_SOURCE_DIR}")

set(_z7_header_globs
  "include/*.h"
  "include/*.hpp"
  "include/*.hh"
  "src/*/include/*.h"
  "src/*/include/*.hpp"
  "src/*/include/*.hh"
  "src/*/*/include/*.h"
  "src/*/*/include/*.hpp"
  "src/*/*/include/*.hh")

set(_z7_symbol_regex
  "\\b(AString|UString|CCodecs|CArc|CArchiveLink|CProxyDir|CProxyDir2|IInArchive|CHashBundle|CUpdateErrorInfo|PROPID|UInt32|UInt64|Int32)\\b")

set(_z7_headers)
foreach(_z7_glob IN LISTS _z7_header_globs)
  file(GLOB_RECURSE _z7_found RELATIVE "${_z7_root}" "${_z7_glob}")
  list(APPEND _z7_headers ${_z7_found})
endforeach()
list(REMOVE_DUPLICATES _z7_headers)

set(_z7_violations)

foreach(_z7_rel IN LISTS _z7_headers)
  if(_z7_rel MATCHES "^src/third_party/")
    continue()
  endif()

  set(_z7_abs "${_z7_root}/${_z7_rel}")
  if(NOT EXISTS "${_z7_abs}")
    continue()
  endif()

  file(READ "${_z7_abs}" _z7_content)
  if(_z7_content MATCHES "${_z7_symbol_regex}")
    string(REGEX MATCHALL "${_z7_symbol_regex}" _z7_hits "${_z7_content}")
    list(REMOVE_DUPLICATES _z7_hits)
    list(JOIN _z7_hits ", " _z7_hit_text)
    list(APPEND _z7_violations "${_z7_rel}: ${_z7_hit_text}")
  endif()
endforeach()

if(_z7_violations)
  list(JOIN _z7_violations "\n  " _z7_violation_text)
  message(FATAL_ERROR
    "Public headers expose third-party internal symbols:\n"
    "  ${_z7_violation_text}\n"
    "Move such dependencies into bridge/private implementation layers.")
endif()

message(STATUS "Third-party symbol boundary check passed.")
