include_guard(GLOBAL)

set(_z7_boundary_script_dir "${CMAKE_CURRENT_LIST_DIR}")

set(_z7_boundary_common_args
  -DPROJECT_SOURCE_DIR:PATH=${CMAKE_SOURCE_DIR})

add_custom_target(z7_verify_third_party_boundary
  COMMAND "${CMAKE_COMMAND}" ${_z7_boundary_common_args}
          -P "${_z7_boundary_script_dir}/VerifyThirdPartyBoundary.cmake"
  COMMENT "z7 boundary checks: verifying third-party include boundary"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_verify_native_bridge_usage
  COMMAND "${CMAKE_COMMAND}" ${_z7_boundary_common_args}
          -P "${_z7_boundary_script_dir}/VerifyNativeBridgeUsage.cmake"
  COMMENT "z7 boundary checks: verifying native bridge usage"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_verify_third_party_symbol_boundary
  COMMAND "${CMAKE_COMMAND}" ${_z7_boundary_common_args}
          -P "${_z7_boundary_script_dir}/VerifyThirdPartySymbolBoundary.cmake"
  COMMENT "z7 boundary checks: verifying third-party symbol boundary"
  USES_TERMINAL
  VERBATIM)

add_custom_target(z7_boundary_checks
  DEPENDS
    z7_verify_third_party_boundary
    z7_verify_native_bridge_usage
    z7_verify_third_party_symbol_boundary)
