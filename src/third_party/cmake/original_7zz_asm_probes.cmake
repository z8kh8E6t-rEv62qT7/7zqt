macro(z7_remove_native_source source_path)
  list(FIND Z7_NATIVE_SOURCES "${source_path}" _z7_remove_index)
  if(_z7_remove_index GREATER -1)
    list(REMOVE_ITEM Z7_NATIVE_SOURCES "${source_path}")
  else()
    message(WARNING "Asm replacement source not found in native list: ${source_path}")
  endif()
endmacro()

macro(z7_register_asm_object c_source object_path remove_c_source)
  if(${remove_c_source})
    z7_remove_native_source("${c_source}")
  endif()
  list(APPEND Z7_ORIGINAL_CORE_COMMON_EXTRA_OBJECTS "${object_path}")
  set(Z7_ORIGINAL_ASM_ENABLED TRUE)
  if("${c_source}" STREQUAL "${Z7_ORIGINAL_VENDOR_DIR}/C/LzmaDec.c")
    set(Z7_ORIGINAL_LZMA_DEC_OPT_ENABLED TRUE)
  endif()
endmacro()

macro(z7_probe_external_asm asm_tool asm_source asm_object out_ok out_log)
  get_filename_component(_z7_source_dir "${asm_source}" DIRECTORY)
  get_filename_component(_z7_source_name "${asm_source}" NAME)
  get_filename_component(_z7_obj_dir "${asm_object}" DIRECTORY)

  file(MAKE_DIRECTORY "${_z7_obj_dir}")
  file(REMOVE "${asm_object}")

  set(_z7_cmd "${asm_tool}" -nologo)
  if(MINGW)
    if(Z7_TARGET_ARCH_X64)
      list(APPEND _z7_cmd -win64)
    else()
      list(APPEND _z7_cmd -coff -DABI_CDECL)
    endif()
    list(APPEND _z7_cmd "-Fo${asm_object}")
  else()
    if(Z7_TARGET_ARCH_X64)
      list(APPEND _z7_cmd -elf64 -DABI_LINUX)
    else()
      list(APPEND _z7_cmd -elf -DABI_LINUX -DABI_CDECL)
    endif()
    list(APPEND _z7_cmd "-Fo${_z7_obj_dir}/")
  endif()
  list(APPEND _z7_cmd "${_z7_source_name}")

  execute_process(
    COMMAND ${_z7_cmd}
    WORKING_DIRECTORY "${_z7_source_dir}"
    RESULT_VARIABLE _z7_result
    OUTPUT_VARIABLE _z7_output
    ERROR_VARIABLE _z7_error)

  if(_z7_result EQUAL 0 AND EXISTS "${asm_object}")
    set(${out_ok} TRUE)
    set(${out_log} "")
  else()
    string(STRIP "${_z7_output}" _z7_output)
    string(STRIP "${_z7_error}" _z7_error)
    set(${out_ok} FALSE)
    set(${out_log}
      "rc=${_z7_result}; out='${_z7_output}'; err='${_z7_error}'")
  endif()
endmacro()

macro(z7_add_external_asm_build asm_tool asm_source asm_object)
  get_filename_component(_z7_source_dir "${asm_source}" DIRECTORY)
  get_filename_component(_z7_source_name "${asm_source}" NAME)
  get_filename_component(_z7_obj_dir "${asm_object}" DIRECTORY)

  set(_z7_cmd "${asm_tool}" -nologo)
  if(MINGW)
    if(Z7_TARGET_ARCH_X64)
      list(APPEND _z7_cmd -win64)
    else()
      list(APPEND _z7_cmd -coff -DABI_CDECL)
    endif()
    list(APPEND _z7_cmd "-Fo${asm_object}")
  else()
    if(Z7_TARGET_ARCH_X64)
      list(APPEND _z7_cmd -elf64 -DABI_LINUX)
    else()
      list(APPEND _z7_cmd -elf -DABI_LINUX -DABI_CDECL)
    endif()
    list(APPEND _z7_cmd "-Fo${_z7_obj_dir}/")
  endif()
  list(APPEND _z7_cmd "${_z7_source_name}")

  add_custom_command(
    OUTPUT "${asm_object}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_z7_obj_dir}"
    COMMAND ${_z7_cmd}
    WORKING_DIRECTORY "${_z7_source_dir}"
    DEPENDS
      "${asm_source}"
      "${Z7_ORIGINAL_VENDOR_DIR}/Asm/x86/7zAsm.asm"
    VERBATIM)
endmacro()

macro(z7_probe_c_compiler_asm asm_source asm_object out_ok out_log)
  get_filename_component(_z7_source_dir "${asm_source}" DIRECTORY)
  get_filename_component(_z7_obj_dir "${asm_object}" DIRECTORY)

  file(MAKE_DIRECTORY "${_z7_obj_dir}")
  file(REMOVE "${asm_object}")

  set(_z7_cmd
    ${Z7_ORIGINAL_C_ASM_COMPILER_CMD}
    -c
    -x
    assembler-with-cpp
    "${asm_source}"
    -o
    "${asm_object}")

  execute_process(
    COMMAND ${_z7_cmd}
    WORKING_DIRECTORY "${_z7_source_dir}"
    RESULT_VARIABLE _z7_result
    OUTPUT_VARIABLE _z7_output
    ERROR_VARIABLE _z7_error)

  if(_z7_result EQUAL 0 AND EXISTS "${asm_object}")
    set(${out_ok} TRUE)
    set(${out_log} "")
  else()
    string(STRIP "${_z7_output}" _z7_output)
    string(STRIP "${_z7_error}" _z7_error)
    set(${out_ok} FALSE)
    set(${out_log}
      "rc=${_z7_result}; out='${_z7_output}'; err='${_z7_error}'")
  endif()
endmacro()

macro(z7_add_c_compiler_asm_build asm_source asm_object)
  get_filename_component(_z7_source_dir "${asm_source}" DIRECTORY)
  get_filename_component(_z7_obj_dir "${asm_object}" DIRECTORY)

  set(_z7_depends "${asm_source}")
  if(EXISTS "${_z7_source_dir}/7zAsm.S")
    list(APPEND _z7_depends "${_z7_source_dir}/7zAsm.S")
  endif()

  add_custom_command(
    OUTPUT "${asm_object}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_z7_obj_dir}"
    COMMAND
      ${Z7_ORIGINAL_C_ASM_COMPILER_CMD}
      -c
      -x
      assembler-with-cpp
      "${asm_source}"
      -o
      "${asm_object}"
    WORKING_DIRECTORY "${_z7_source_dir}"
    DEPENDS ${_z7_depends}
    VERBATIM)
endmacro()
