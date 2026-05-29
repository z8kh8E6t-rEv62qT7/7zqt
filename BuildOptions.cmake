include_guard(GLOBAL)

# Project-wide build option defaults. Priority is:
# 1. command-line -D... / existing cache
# 2. defaults initialized in this file
# 3. option() fallback defaults in cmake/Z7QtOptions.cmake
# This file is tracked by git; edits change the team-wide default.
set(Z7_BUILD_TESTS ON CACHE BOOL "Build unit/integration tests.")

if(APPLE)
  set(Z7_ENABLE_MACOS_INTEGRATION ON CACHE BOOL
      "Enable macOS integration components.")
endif()
