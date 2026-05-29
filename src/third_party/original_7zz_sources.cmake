include_guard(GLOBAL)

if(NOT DEFINED Z7_ORIGINAL_VENDOR_DIR)
  set(Z7_ORIGINAL_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/original_7zip")
endif()

if(NOT EXISTS "${Z7_ORIGINAL_VENDOR_DIR}/C" OR
   NOT EXISTS "${Z7_ORIGINAL_VENDOR_DIR}/CPP")
  message(FATAL_ERROR
    "Vendored 7-Zip tree is incomplete: ${Z7_ORIGINAL_VENDOR_DIR}\n"
    "Expected at least:\n"
    "  ${Z7_ORIGINAL_VENDOR_DIR}/C\n"
    "  ${Z7_ORIGINAL_VENDOR_DIR}/CPP")
endif()

set(Z7_ORIGINAL_THIRD_PARTY_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(Z7_ORIGINAL_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake")

include("${Z7_ORIGINAL_CMAKE_DIR}/original_7zz_snapshot_sources.cmake")
include("${Z7_ORIGINAL_CMAKE_DIR}/original_7zz_validation_and_asm.cmake")
include("${Z7_ORIGINAL_CMAKE_DIR}/original_7zz_targets.cmake")
