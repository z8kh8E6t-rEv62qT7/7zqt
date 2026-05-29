set(Z7_ORIGINAL_SNAPSHOT_SOURCE_COUNT 321)

if(NOT Z7_NATIVE_BASE_SOURCES)
  set(Z7_NATIVE_BASE_SOURCES ${Z7_NATIVE_SOURCES})
endif()

if(NOT Z7_NATIVE_BASE_SOURCES)
  message(FATAL_ERROR
    "Explicit original 7zz source list is empty. "
    "Check src/third_party/original_7zz_sources.cmake.")
endif()

set(Z7_NATIVE_SOURCES ${Z7_NATIVE_BASE_SOURCES})

set(Z7_NATIVE_SOURCES_MISSING)
set(Z7_NATIVE_SOURCES_SEEN)
set(Z7_NATIVE_SOURCES_DUPLICATES)

foreach(src IN LISTS Z7_NATIVE_BASE_SOURCES)
  if(NOT EXISTS "${src}")
    list(APPEND Z7_NATIVE_SOURCES_MISSING "${src}")
  endif()

  if(src IN_LIST Z7_NATIVE_SOURCES_SEEN)
    list(APPEND Z7_NATIVE_SOURCES_DUPLICATES "${src}")
  else()
    list(APPEND Z7_NATIVE_SOURCES_SEEN "${src}")
  endif()
endforeach()

if(Z7_NATIVE_SOURCES_MISSING)
  list(JOIN Z7_NATIVE_SOURCES_MISSING "\n  " Z7_NATIVE_SOURCES_MISSING_MESSAGE)
  message(FATAL_ERROR
    "Explicit original 7zz source list contains missing files:\n"
    "  ${Z7_NATIVE_SOURCES_MISSING_MESSAGE}")
endif()

if(Z7_NATIVE_SOURCES_DUPLICATES)
  list(JOIN Z7_NATIVE_SOURCES_DUPLICATES "\n  " Z7_NATIVE_SOURCES_DUPLICATES_MESSAGE)
  message(FATAL_ERROR
    "Explicit original 7zz source list contains duplicate entries:\n"
    "  ${Z7_NATIVE_SOURCES_DUPLICATES_MESSAGE}")
endif()

list(LENGTH Z7_NATIVE_BASE_SOURCES Z7_NATIVE_SOURCE_COUNT)
if(NOT Z7_NATIVE_SOURCE_COUNT EQUAL Z7_ORIGINAL_SNAPSHOT_SOURCE_COUNT)
  message(FATAL_ERROR
    "Explicit original 7zz source list size changed: expected "
    "${Z7_ORIGINAL_SNAPSHOT_SOURCE_COUNT}, got ${Z7_NATIVE_SOURCE_COUNT}. "
    "Update src/third_party/original_7zz_sources.cmake "
    "when syncing upstream source changes.")
endif()

macro(z7_check_parallel_lists list_a list_b label)
  list(LENGTH ${list_a} _z7_len_a)
  list(LENGTH ${list_b} _z7_len_b)
  if(NOT _z7_len_a EQUAL _z7_len_b)
    message(FATAL_ERROR
      "${label} mapping list size mismatch: ${list_a}=${_z7_len_a}, "
      "${list_b}=${_z7_len_b}")
  endif()
endmacro()

z7_check_parallel_lists(Z7_ORIGINAL_ASM_X86_REPLACE_C_SOURCES
  Z7_ORIGINAL_ASM_X86_SOURCES
  "x86 asm")
z7_check_parallel_lists(Z7_ORIGINAL_ASM_X64_REPLACE_C_SOURCES
  Z7_ORIGINAL_ASM_X64_SOURCES
  "x64 asm")
z7_check_parallel_lists(Z7_ORIGINAL_ASM_ARM64_REPLACE_C_SOURCES
  Z7_ORIGINAL_ASM_ARM64_SOURCES
  "arm64 asm")
z7_check_parallel_lists(Z7_ORIGINAL_ASM_ARM_REPLACE_C_SOURCES
  Z7_ORIGINAL_ASM_ARM_SOURCES
  "arm asm")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" Z7_SYSTEM_PROCESSOR_LOWER)
set(Z7_TARGET_ARCH_X64 FALSE)
set(Z7_TARGET_ARCH_X86 FALSE)
set(Z7_TARGET_ARCH_ARM64 FALSE)
set(Z7_TARGET_ARCH_ARM FALSE)

if(Z7_SYSTEM_PROCESSOR_LOWER MATCHES "^(x86_64|amd64|x64)$")
  set(Z7_TARGET_ARCH_X64 TRUE)
elseif(Z7_SYSTEM_PROCESSOR_LOWER MATCHES "^(x86|i[3-6]86)$")
  set(Z7_TARGET_ARCH_X86 TRUE)
elseif(Z7_SYSTEM_PROCESSOR_LOWER MATCHES "^(aarch64|arm64)$")
  set(Z7_TARGET_ARCH_ARM64 TRUE)
elseif(Z7_SYSTEM_PROCESSOR_LOWER MATCHES "^arm")
  set(Z7_TARGET_ARCH_ARM TRUE)
endif()

set(Z7_ORIGINAL_CORE_COMMON_EXTRA_OBJECTS)
set(Z7_ORIGINAL_ASM_ENABLED FALSE)
set(Z7_ORIGINAL_LZMA_DEC_OPT_ENABLED FALSE)
set(Z7_ORIGINAL_ASM_STATUS)
set(Z7_ORIGINAL_X86_ASM_REQUIRED FALSE)
set(Z7_ORIGINAL_X64_ASM_REQUIRED FALSE)
set(Z7_ORIGINAL_ARM64_ASM_REQUIRED FALSE)
set(Z7_ORIGINAL_ARM_ASM_REQUIRED FALSE)

set(Z7_ORIGINAL_X86_ASM_TOOL "")
if(Z7_ORIGINAL_X86_ASM_COMPILER)
  if(IS_ABSOLUTE "${Z7_ORIGINAL_X86_ASM_COMPILER}")
    if(EXISTS "${Z7_ORIGINAL_X86_ASM_COMPILER}")
      set(Z7_ORIGINAL_X86_ASM_TOOL "${Z7_ORIGINAL_X86_ASM_COMPILER}")
    endif()
  else()
    find_program(Z7_ORIGINAL_X86_ASM_TOOL
      NAMES "${Z7_ORIGINAL_X86_ASM_COMPILER}")
  endif()

  if(NOT Z7_ORIGINAL_X86_ASM_TOOL)
    message(FATAL_ERROR
      "Z7_ORIGINAL_X86_ASM_COMPILER was specified but not found: "
      "${Z7_ORIGINAL_X86_ASM_COMPILER}")
  endif()
endif()

if((Z7_TARGET_ARCH_X86 OR Z7_TARGET_ARCH_X64) AND
   WIN32 AND NOT MINGW)
  if(Z7_ORIGINAL_X86_ASM_COMPILER)
    message(FATAL_ERROR
      "x86/x64 Asm support in this project is limited to MinGW on Windows. "
      "Current toolchain is not MinGW.")
  endif()
  list(APPEND Z7_ORIGINAL_ASM_STATUS
    "x86/x64 asm skipped on non-MinGW Windows toolchain")
endif()

set(_z7_c_asm_flags "${CMAKE_C_FLAGS}")
if(CMAKE_BUILD_TYPE)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _z7_build_type_upper)
  if(DEFINED CMAKE_C_FLAGS_${_z7_build_type_upper})
    string(APPEND _z7_c_asm_flags " ${CMAKE_C_FLAGS_${_z7_build_type_upper}}")
  endif()
endif()
separate_arguments(Z7_ORIGINAL_C_ASM_EXTRA_FLAGS NATIVE_COMMAND "${_z7_c_asm_flags}")

set(Z7_ORIGINAL_C_ASM_COMPILER_CMD "${CMAKE_C_COMPILER}")
if(CMAKE_C_COMPILER_ARG1)
  list(APPEND Z7_ORIGINAL_C_ASM_COMPILER_CMD "${CMAKE_C_COMPILER_ARG1}")
endif()
if(CMAKE_C_COMPILER_TARGET)
  list(APPEND Z7_ORIGINAL_C_ASM_COMPILER_CMD "--target=${CMAKE_C_COMPILER_TARGET}")
endif()
if(CMAKE_SYSROOT)
  list(APPEND Z7_ORIGINAL_C_ASM_COMPILER_CMD "--sysroot=${CMAKE_SYSROOT}")
endif()
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
  list(APPEND Z7_ORIGINAL_C_ASM_COMPILER_CMD
    "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
list(APPEND Z7_ORIGINAL_C_ASM_COMPILER_CMD ${Z7_ORIGINAL_C_ASM_EXTRA_FLAGS})
