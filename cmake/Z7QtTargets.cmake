include_guard(GLOBAL)

set(Z7_MACOS_USE_HOMEBREW_LLVM_RUNTIME FALSE)
set(Z7_MACOS_RELEASE_RUNTIME_ENFORCEMENT FALSE)
set(Z7_MACOS_LLVM_RESOURCE_DIR "")
set(Z7_MACOS_LLVM_ROOT "")
set(Z7_MACOS_LLVM_LIBCXX "")
set(Z7_MACOS_LLVM_LIBCXXABI "")
set(Z7_MACOS_LLVM_LIBUNWIND "")
set(Z7_MACOS_LLVM_CLANG_RT_OSX "")
set(Z7_MACOS_LD64_LLD "")

if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set(Z7_MACOS_RELEASE_RUNTIME_ENFORCEMENT TRUE)
endif()

function(_z7_macos_compiler_version output_variable compiler_path)
  execute_process(
    COMMAND "${compiler_path}" --version
    RESULT_VARIABLE _z7_compiler_version_rc
    OUTPUT_VARIABLE _z7_compiler_version
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _z7_compiler_version_rc EQUAL 0)
    set(${output_variable} "" PARENT_SCOPE)
    return()
  endif()
  set(${output_variable} "${_z7_compiler_version}" PARENT_SCOPE)
endfunction()

function(_z7_macos_homebrew_llvm_root_from_compiler output_variable compiler_path)
  set(_z7_llvm_root_candidates "")
  if(DEFINED Z7_PREFERRED_HOMEBREW_LLVM_ROOT)
    list(APPEND _z7_llvm_root_candidates "${Z7_PREFERRED_HOMEBREW_LLVM_ROOT}")
  endif()

  get_filename_component(_z7_compiler_real "${compiler_path}" REALPATH)
  get_filename_component(_z7_compiler_bin_dir "${_z7_compiler_real}" DIRECTORY)
  get_filename_component(_z7_compiler_root "${_z7_compiler_bin_dir}" DIRECTORY)
  list(APPEND _z7_llvm_root_candidates
    "${_z7_compiler_root}"
    "/opt/homebrew/opt/llvm"
    "/usr/local/opt/llvm")

  list(REMOVE_DUPLICATES _z7_llvm_root_candidates)
  foreach(_z7_llvm_root IN LISTS _z7_llvm_root_candidates)
    if(EXISTS "${_z7_llvm_root}/bin/clang" AND
       EXISTS "${_z7_llvm_root}/bin/clang++" AND
       EXISTS "${_z7_llvm_root}/lib/c++/libc++.1.dylib" AND
       EXISTS "${_z7_llvm_root}/lib/c++/libc++abi.1.dylib" AND
       EXISTS "${_z7_llvm_root}/lib/unwind/libunwind.1.dylib")
      set(${output_variable} "${_z7_llvm_root}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${output_variable} "" PARENT_SCOPE)
endfunction()

if(APPLE AND Z7_MACOS_RELEASE_RUNTIME_ENFORCEMENT)
  _z7_macos_compiler_version(_z7_cc_version "${CMAKE_C_COMPILER}")
  _z7_macos_compiler_version(_z7_cxx_version "${CMAKE_CXX_COMPILER}")

  if(NOT _z7_cc_version MATCHES "Homebrew clang version" OR
     NOT _z7_cxx_version MATCHES "Homebrew clang version")
    message(FATAL_ERROR
      "macOS Release builds must use Homebrew LLVM clang/clang++.\n"
      "  C compiler: ${CMAKE_C_COMPILER}\n"
      "  C version: ${_z7_cc_version}\n"
      "  C++ compiler: ${CMAKE_CXX_COMPILER}\n"
      "  C++ version: ${_z7_cxx_version}")
  endif()

  _z7_macos_homebrew_llvm_root_from_compiler(
    _z7_llvm_root
    "${CMAKE_CXX_COMPILER}")
  if(_z7_llvm_root STREQUAL "")
    message(FATAL_ERROR
      "macOS Release builds require Homebrew LLVM runtime dylibs next to the "
      "selected compiler or under /opt/homebrew/opt/llvm or /usr/local/opt/llvm.")
  endif()

  execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
    RESULT_VARIABLE _z7_resource_rc
    OUTPUT_VARIABLE _z7_resource_dir
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _z7_resource_rc EQUAL 0 OR _z7_resource_dir STREQUAL "")
    message(FATAL_ERROR
      "macOS Release builds require a Homebrew LLVM resource directory from "
      "${CMAKE_C_COMPILER} -print-resource-dir.")
  endif()

  set(_z7_resource_lib_dir "${_z7_resource_dir}/lib/darwin")
  set(_z7_clang_rt_osx_lib "${_z7_resource_lib_dir}/libclang_rt.osx.a")
  set(_z7_libcxx "${_z7_llvm_root}/lib/c++/libc++.1.dylib")
  set(_z7_libcxxabi "${_z7_llvm_root}/lib/c++/libc++abi.1.dylib")
  set(_z7_libunwind "${_z7_llvm_root}/lib/unwind/libunwind.1.dylib")

  foreach(_z7_required_runtime IN ITEMS
      "${_z7_clang_rt_osx_lib}"
      "${_z7_libcxx}"
      "${_z7_libcxxabi}"
      "${_z7_libunwind}")
    if(NOT EXISTS "${_z7_required_runtime}")
      message(FATAL_ERROR
        "Required Homebrew LLVM Release runtime path is missing: "
        "${_z7_required_runtime}")
    endif()
  endforeach()

  set(Z7_MACOS_USE_HOMEBREW_LLVM_RUNTIME TRUE)
  set(Z7_MACOS_LLVM_RESOURCE_DIR "${_z7_resource_dir}")
  set(Z7_MACOS_LLVM_ROOT "${_z7_llvm_root}")
  set(Z7_MACOS_LLVM_LIBCXX "${_z7_libcxx}")
  set(Z7_MACOS_LLVM_LIBCXXABI "${_z7_libcxxabi}")
  set(Z7_MACOS_LLVM_LIBUNWIND "${_z7_libunwind}")
  set(Z7_MACOS_LLVM_CLANG_RT_OSX "${_z7_clang_rt_osx_lib}")

  message(STATUS
    "z7qt: enforcing explicit Homebrew LLVM runtime for macOS Release "
    "(resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}, llvm-root=${Z7_MACOS_LLVM_ROOT})")
endif()

if(APPLE AND Z7_MACOS_RELEASE_TUNING AND Z7_MACOS_LLVM_ROOT)
  find_program(_z7_ld64_lld
    NAMES ld64.lld
    HINTS
      "${Z7_MACOS_LLVM_ROOT}/bin"
      "/opt/homebrew/bin"
      "/usr/local/bin")
  if(_z7_ld64_lld)
    set(Z7_MACOS_LD64_LLD "${_z7_ld64_lld}")
    message(STATUS "z7qt: enabling ld64.lld for macOS Release links: ${Z7_MACOS_LD64_LLD}")
  else()
    message(STATUS "z7qt: ld64.lld not found, using default macOS linker")
  endif()
endif()

function(z7_set_default_target_options target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)

  if(Z7_STRICT_WARNINGS)
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
      target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()

  if(APPLE AND Z7_MACOS_RELEASE_RUNTIME_ENFORCEMENT)
    target_compile_options(${target_name} PRIVATE
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C>>:-rtlib=compiler-rt>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-rtlib=compiler-rt>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:OBJC>>:-rtlib=compiler-rt>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:OBJCXX>>:-rtlib=compiler-rt>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C>>:-resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:OBJC>>:-resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:OBJCXX>>:-resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-stdlib=libc++>"
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:OBJCXX>>:-stdlib=libc++>")
  endif()

  if(APPLE AND Z7_MACOS_RELEASE_TUNING)
    target_compile_options(${target_name} PRIVATE
      "$<$<CONFIG:Release>:-O3>"
      "$<$<CONFIG:Release>:-pipe>"
      "$<$<CONFIG:Release>:-fno-math-errno>"
      "$<$<CONFIG:Release>:-falign-functions=32>"
      "$<$<CONFIG:Release>:-fstrict-aliasing>"
      "$<$<CONFIG:Release>:-ffunction-sections>"
      "$<$<CONFIG:Release>:-fdata-sections>"
      "$<$<CONFIG:Release>:-fveclib=Accelerate>")

    if(Z7_MACOS_HOST_NATIVE_OPT)
      target_compile_options(${target_name} PRIVATE
        "$<$<CONFIG:Release>:-mcpu=native>"
        "$<$<CONFIG:Release>:-mtune=native>"
        "$<$<CONFIG:Release>:-march=native>")
    endif()

    if(Z7_MACOS_LD64_LLD)
      target_compile_options(${target_name} PRIVATE
        "$<$<CONFIG:Release>:-flto=thin>")
    endif()
    get_target_property(_z7_target_type "${target_name}" TYPE)
    if(_z7_target_type STREQUAL "EXECUTABLE" OR
       _z7_target_type STREQUAL "SHARED_LIBRARY" OR
       _z7_target_type STREQUAL "MODULE_LIBRARY")
      target_link_options(${target_name} PRIVATE
        "$<$<CONFIG:Release>:-Wl,-dead_strip>")

      if(Z7_MACOS_LD64_LLD)
        target_link_options(${target_name} PRIVATE
          "$<$<CONFIG:Release>:-fuse-ld=${Z7_MACOS_LD64_LLD}>"
          "$<$<CONFIG:Release>:-Wl,--icf=all>"
          "$<$<CONFIG:Release>:-flto=thin>")
      endif()
    endif()
  endif()

  get_target_property(_z7_target_type "${target_name}" TYPE)
  if(APPLE AND Z7_MACOS_RELEASE_RUNTIME_ENFORCEMENT AND
     (_z7_target_type STREQUAL "EXECUTABLE" OR
      _z7_target_type STREQUAL "SHARED_LIBRARY" OR
      _z7_target_type STREQUAL "MODULE_LIBRARY"))
    target_link_options(${target_name} PRIVATE
      "$<$<CONFIG:Release>:-rtlib=compiler-rt>"
      "$<$<CONFIG:Release>:-resource-dir=${Z7_MACOS_LLVM_RESOURCE_DIR}>"
      "$<$<CONFIG:Release>:-nostdlib++>")
    target_link_libraries(${target_name} PRIVATE
      "$<$<CONFIG:Release>:${Z7_MACOS_LLVM_CLANG_RT_OSX}>"
      "$<$<CONFIG:Release>:${Z7_MACOS_LLVM_LIBUNWIND}>"
      "$<$<CONFIG:Release>:${Z7_MACOS_LLVM_LIBCXX}>"
      "$<$<CONFIG:Release>:${Z7_MACOS_LLVM_LIBCXXABI}>")
    add_custom_command(TARGET "${target_name}" POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}"
        -DZ7_MACOS_LLVM_RUNTIME_VALIDATE_RUNNER=ON
        -DZ7_MACOS_VALIDATE_ENABLE=$<CONFIG:Release>
        "-DZ7_MACOS_VALIDATE_FILE=$<TARGET_FILE:${target_name}>"
        -DZ7_MACOS_VALIDATE_REQUIRE_LLVM_RUNTIME=ON
        "-DZ7_MACOS_VALIDATE_LLVM_ROOT=${Z7_MACOS_LLVM_ROOT}"
        -P "${PROJECT_SOURCE_DIR}/cmake/Z7MacOSLlvmRuntimeValidation.cmake"
      VERBATIM)
  endif()

  z7_apply_llvm_coverage_to_target("${target_name}")
endfunction()
