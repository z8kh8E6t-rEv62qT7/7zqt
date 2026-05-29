if((Z7_TARGET_ARCH_X86 OR Z7_TARGET_ARCH_X64) AND
   (NOT WIN32 OR MINGW))
  if(Z7_TARGET_ARCH_X64)
    set(Z7_ORIGINAL_X64_ASM_REQUIRED TRUE)
  else()
    set(Z7_ORIGINAL_X86_ASM_REQUIRED TRUE)
  endif()

  if(NOT Z7_ORIGINAL_X86_ASM_TOOL)
    find_program(Z7_ORIGINAL_X86_ASM_TOOL
      NAMES asmc uasm jwasm)
  endif()

  if(Z7_ORIGINAL_X86_ASM_TOOL)
    set(Z7_ORIGINAL_X86_ANY_ENABLED FALSE)

    set(_z7_index 0)
    foreach(asm_source IN LISTS Z7_ORIGINAL_ASM_X86_SOURCES)
      list(GET Z7_ORIGINAL_ASM_X86_REPLACE_C_SOURCES ${_z7_index} c_source)
      get_filename_component(_z7_stem "${asm_source}" NAME_WE)
      set(asm_object
        "${CMAKE_CURRENT_BINARY_DIR}/original_asm/x86/${_z7_stem}.o")

      z7_probe_external_asm("${Z7_ORIGINAL_X86_ASM_TOOL}" "${asm_source}"
        "${asm_object}" _z7_ok _z7_log)
      if(_z7_ok)
        z7_add_external_asm_build("${Z7_ORIGINAL_X86_ASM_TOOL}" "${asm_source}"
          "${asm_object}")
        z7_register_asm_object("${c_source}" "${asm_object}" TRUE)
        set(Z7_ORIGINAL_X86_ANY_ENABLED TRUE)
        list(APPEND Z7_ORIGINAL_ASM_STATUS "enabled x86 asm: ${asm_source}")
      else()
        list(APPEND Z7_ORIGINAL_ASM_STATUS
          "switch to C for ${asm_source} (${_z7_log})")
      endif()

      math(EXPR _z7_index "${_z7_index} + 1")
    endforeach()

    if(Z7_TARGET_ARCH_X64)
      set(_z7_index 0)
      foreach(asm_source IN LISTS Z7_ORIGINAL_ASM_X64_SOURCES)
        list(GET Z7_ORIGINAL_ASM_X64_REPLACE_C_SOURCES ${_z7_index} c_source)
        get_filename_component(_z7_stem "${asm_source}" NAME_WE)
        set(asm_object
          "${CMAKE_CURRENT_BINARY_DIR}/original_asm/x64/${_z7_stem}.o")

        z7_probe_external_asm("${Z7_ORIGINAL_X86_ASM_TOOL}" "${asm_source}"
          "${asm_object}" _z7_ok _z7_log)
        if(_z7_ok)
          z7_add_external_asm_build("${Z7_ORIGINAL_X86_ASM_TOOL}" "${asm_source}"
            "${asm_object}")

          set(_z7_remove_c TRUE)
          if("${c_source}" STREQUAL "${Z7_ORIGINAL_VENDOR_DIR}/C/LzmaDec.c")
            set(_z7_remove_c FALSE)
          endif()

          z7_register_asm_object("${c_source}" "${asm_object}" ${_z7_remove_c})
          set(Z7_ORIGINAL_X86_ANY_ENABLED TRUE)
          list(APPEND Z7_ORIGINAL_ASM_STATUS "enabled x64 asm: ${asm_source}")
        else()
          list(APPEND Z7_ORIGINAL_ASM_STATUS
            "switch to C for ${asm_source} (${_z7_log})")
        endif()

        math(EXPR _z7_index "${_z7_index} + 1")
      endforeach()
    endif()

    if(NOT Z7_ORIGINAL_X86_ANY_ENABLED)
      message(FATAL_ERROR
        "x86/x64 Asm is required on this supported platform, but assembler "
        "'${Z7_ORIGINAL_X86_ASM_TOOL}' could not compile any candidate Asm source.")
    endif()
  else()
    if(Z7_ORIGINAL_X86_ASM_COMPILER)
      message(FATAL_ERROR
        "Z7_ORIGINAL_X86_ASM_COMPILER was specified but could not be used.")
    endif()
    message(FATAL_ERROR
      "x86/x64 Asm is required on this supported platform, but no assembler "
      "was found (tried: asmc/uasm/jwasm).")
  endif()
endif()

if(Z7_TARGET_ARCH_ARM64)
  set(Z7_ORIGINAL_ARM64_ASM_REQUIRED TRUE)
  list(GET Z7_ORIGINAL_ASM_ARM64_SOURCES 0 asm_source)
  list(GET Z7_ORIGINAL_ASM_ARM64_REPLACE_C_SOURCES 0 c_source)
  get_filename_component(_z7_stem "${asm_source}" NAME_WE)
  set(asm_object
    "${CMAKE_CURRENT_BINARY_DIR}/original_asm/arm64/${_z7_stem}.o")

  z7_probe_c_compiler_asm("${asm_source}" "${asm_object}" _z7_ok _z7_log)
  if(_z7_ok)
    z7_add_c_compiler_asm_build("${asm_source}" "${asm_object}")
    z7_register_asm_object("${c_source}" "${asm_object}" FALSE)
    list(APPEND Z7_ORIGINAL_ASM_STATUS "enabled arm64 asm: ${asm_source}")
  else()
    message(FATAL_ERROR
      "arm64 Asm is required on arm64 platform, but probe failed for "
      "${asm_source}: ${_z7_log}")
  endif()
endif()

if(Z7_TARGET_ARCH_ARM)
  set(Z7_ORIGINAL_ARM_ASM_REQUIRED TRUE)
  list(GET Z7_ORIGINAL_ASM_ARM_SOURCES 0 asm_source)
  list(GET Z7_ORIGINAL_ASM_ARM_REPLACE_C_SOURCES 0 c_source)
  get_filename_component(_z7_stem "${asm_source}" NAME_WE)
  set(asm_object
    "${CMAKE_CURRENT_BINARY_DIR}/original_asm/arm/${_z7_stem}.o")

  z7_probe_c_compiler_asm("${asm_source}" "${asm_object}" _z7_ok _z7_log)
  if(_z7_ok)
    z7_add_c_compiler_asm_build("${asm_source}" "${asm_object}")
    z7_register_asm_object("${c_source}" "${asm_object}" TRUE)
    list(APPEND Z7_ORIGINAL_ASM_STATUS "enabled arm asm: ${asm_source}")
  else()
    message(FATAL_ERROR
      "arm Asm is required on arm platform, but probe failed for "
      "${asm_source}: ${_z7_log}")
  endif()
endif()

if(Z7_ORIGINAL_X86_ASM_REQUIRED OR Z7_ORIGINAL_X64_ASM_REQUIRED OR
   Z7_ORIGINAL_ARM64_ASM_REQUIRED OR Z7_ORIGINAL_ARM_ASM_REQUIRED)
  if(NOT Z7_ORIGINAL_ASM_ENABLED)
    message(FATAL_ERROR
      "Asm is required for this platform, but no Asm object was enabled.")
  endif()
endif()

if(Z7_ORIGINAL_ARM64_ASM_REQUIRED AND NOT Z7_ORIGINAL_LZMA_DEC_OPT_ENABLED)
  message(FATAL_ERROR
    "arm64 Asm is required, but LzmaDecOpt Asm path was not enabled.")
endif()

if(Z7_ORIGINAL_ASM_STATUS)
  list(JOIN Z7_ORIGINAL_ASM_STATUS "\n  " Z7_ORIGINAL_ASM_STATUS_MSG)
  message(STATUS "Original 7-Zip Asm selection:\n  ${Z7_ORIGINAL_ASM_STATUS_MSG}")
endif()
