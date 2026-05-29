cmake_minimum_required(VERSION 3.31)

set(Z7_THIRD_PARTY_STANDALONE FALSE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(Z7_THIRD_PARTY_STANDALONE TRUE)
  project(z7_third_party LANGUAGES C CXX)
endif()

option(Z7_ENABLE_INSTALL "Enable install targets." ON)
set(Z7_ORIGINAL_X86_ASM_COMPILER "" CACHE FILEPATH
    "Optional assembler path/name for original_7zip x86/x64 Asm (asmc/uasm/jwasm).")

if(NOT DEFINED Z7_ORIGINAL_VENDOR_DIR)
  set(Z7_ORIGINAL_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/original_7zip")
endif()

if(Z7_THIRD_PARTY_STANDALONE)
  include(GNUInstallDirs)
endif()

if(NOT COMMAND z7_set_default_target_options)
  function(z7_set_default_target_options target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endfunction()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/original_7zz_sources.cmake")
