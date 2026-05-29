include_guard(GLOBAL)

option(Z7_BUILD_TESTS "Build unit/integration tests." ON)
option(Z7_ENABLE_INSTALL "Enable install targets." ON)
option(Z7_STRICT_WARNINGS "Enable strict warning flags." ON)
option(Z7_ENABLE_LLVM_COVERAGE
  "Enable LLVM source-based coverage instrumentation and report targets." OFF)
option(Z7_MACOS_RELEASE_TUNING
  "Enable macOS-only release optimization and linker tuning flags." ON)
option(Z7_MACOS_HOST_NATIVE_OPT
  "Enable host-native ISA flags (-mcpu/-mtune/-march=native) for macOS release builds." OFF)

set(Z7_QT_VERSION_MIN "6.5" CACHE STRING "Minimum required Qt6 version")
set(Z7_QT_ROOT "" CACHE PATH "Optional Qt root that contains Qt6Config.cmake")
set(Z7_THIRD_PARTY_ROOT
    "${PROJECT_SOURCE_DIR}/src/third_party"
    CACHE PATH
    "Root directory that contains third-party 7-Zip module CMakeLists.txt.")
set(Z7_ORIGINAL_X86_ASM_COMPILER "" CACHE FILEPATH
    "Optional assembler path/name for original_7zip x86/x64 Asm (asmc/uasm/jwasm).")

if(NOT IS_ABSOLUTE "${Z7_THIRD_PARTY_ROOT}")
  get_filename_component(Z7_THIRD_PARTY_ROOT
    "${PROJECT_SOURCE_DIR}/${Z7_THIRD_PARTY_ROOT}"
    ABSOLUTE)
  set(Z7_THIRD_PARTY_ROOT "${Z7_THIRD_PARTY_ROOT}" CACHE PATH
      "Root directory that contains third-party 7-Zip module CMakeLists.txt." FORCE)
endif()

set(Z7_THIRD_PARTY_CMAKELISTS "${Z7_THIRD_PARTY_ROOT}/CMakeLists.txt")
set(Z7_ORIGINAL_VENDOR_DIR "${Z7_THIRD_PARTY_ROOT}/original_7zip")
set(Z7_ORIGINAL_SOURCES_CMAKE "${Z7_THIRD_PARTY_ROOT}/original_7zz_sources.cmake")

if(NOT EXISTS "${Z7_THIRD_PARTY_CMAKELISTS}")
  message(FATAL_ERROR
    "Third-party module entry is missing: ${Z7_THIRD_PARTY_CMAKELISTS}\n"
    "Set Z7_THIRD_PARTY_ROOT to a directory that contains third-party CMakeLists.txt.")
endif()

if(NOT EXISTS "${Z7_ORIGINAL_SOURCES_CMAKE}")
  message(FATAL_ERROR
    "Third-party manifest is missing: ${Z7_ORIGINAL_SOURCES_CMAKE}\n"
    "Set Z7_THIRD_PARTY_ROOT to a directory that contains original_7zz_sources.cmake.")
endif()
