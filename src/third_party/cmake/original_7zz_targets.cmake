set(Z7_ORIGINAL_CORE_COMMON_SOURCES)
set(Z7_ORIGINAL_ARCHIVE_API_SOURCES)
set(Z7_ORIGINAL_AGENT_API_SOURCES)
set(Z7_ORIGINAL_CONSOLE_BRIDGE_SOURCES)

foreach(src IN LISTS Z7_NATIVE_SOURCES)
  if("${src}" MATCHES "/CPP/7zip/UI/Console/Main\\.cpp$")
    list(APPEND Z7_ORIGINAL_CONSOLE_BRIDGE_SOURCES "${src}")
  elseif("${src}" MATCHES "/CPP/7zip/UI/Common/")
    list(APPEND Z7_ORIGINAL_ARCHIVE_API_SOURCES "${src}")
  elseif("${src}" MATCHES "/CPP/7zip/UI/Agent/")
    list(APPEND Z7_ORIGINAL_AGENT_API_SOURCES "${src}")
  else()
    list(APPEND Z7_ORIGINAL_CORE_COMMON_SOURCES "${src}")
  endif()
endforeach()

if(Z7_ORIGINAL_LZMA_DEC_OPT_ENABLED)
  set_source_files_properties(
    "${Z7_ORIGINAL_VENDOR_DIR}/C/LzmaDec.c"
    PROPERTIES COMPILE_DEFINITIONS Z7_LZMA_DEC_OPT)
endif()

if(Z7_ORIGINAL_CORE_COMMON_EXTRA_OBJECTS)
  set_source_files_properties(${Z7_ORIGINAL_CORE_COMMON_EXTRA_OBJECTS}
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
  list(APPEND Z7_ORIGINAL_CORE_COMMON_SOURCES
    ${Z7_ORIGINAL_CORE_COMMON_EXTRA_OBJECTS})
endif()

# AgentProxy is required for 7zFM-aligned archive root view semantics.
list(APPEND Z7_ORIGINAL_AGENT_API_SOURCES
  "${Z7_ORIGINAL_VENDOR_DIR}/CPP/7zip/UI/Agent/AgentProxy.cpp")
list(REMOVE_DUPLICATES Z7_ORIGINAL_AGENT_API_SOURCES)

function(z7_configure_original_target target_name)
  target_compile_definitions(${target_name} PRIVATE Z7_PROG_VARIANT_Z)
  if(Z7_ORIGINAL_ASM_ENABLED)
    target_compile_definitions(${target_name} PRIVATE Z7_7ZIP_ASM)
  endif()

  target_include_directories(${target_name}
    PRIVATE
      "${Z7_ORIGINAL_VENDOR_DIR}/CPP"
      "${Z7_ORIGINAL_VENDOR_DIR}")

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target_name} PRIVATE -w)
  endif()
endfunction()

add_library(z7_original_core_common STATIC ${Z7_ORIGINAL_CORE_COMMON_SOURCES})
add_library(z7::original_core_common ALIAS z7_original_core_common)
z7_configure_original_target(z7_original_core_common)
if(TARGET z7_core)
  target_link_libraries(z7_original_core_common PUBLIC z7_core)
elseif(TARGET z7::core)
  target_link_libraries(z7_original_core_common PUBLIC z7::core)
endif()

add_library(z7_original_archive_api STATIC ${Z7_ORIGINAL_ARCHIVE_API_SOURCES})
add_library(z7::original_archive_api ALIAS z7_original_archive_api)
z7_configure_original_target(z7_original_archive_api)
target_link_libraries(z7_original_archive_api PUBLIC z7_original_core_common)

add_library(z7_original_agent_api STATIC ${Z7_ORIGINAL_AGENT_API_SOURCES})
add_library(z7::original_agent_api ALIAS z7_original_agent_api)
z7_configure_original_target(z7_original_agent_api)
target_link_libraries(z7_original_agent_api PUBLIC z7_original_archive_api)

add_library(z7_original_console_bridge STATIC ${Z7_ORIGINAL_CONSOLE_BRIDGE_SOURCES})
add_library(z7::original_console_bridge ALIAS z7_original_console_bridge)
z7_configure_original_target(z7_original_console_bridge)
target_link_libraries(z7_original_console_bridge PUBLIC z7_original_agent_api)
add_library(z7_third_party SHARED
  "${Z7_ORIGINAL_THIRD_PARTY_ROOT}/src/console_stream_globals.cpp")
add_library(z7::third_party ALIAS z7_third_party)
z7_configure_original_target(z7_third_party)
target_link_libraries(z7_third_party
  PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_core_common>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_archive_api>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_agent_api>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,z7_original_console_bridge>")
if(APPLE)
  target_link_libraries(z7_third_party
    PRIVATE
      "-framework CoreFoundation"
      iconv)
endif()
set_target_properties(z7_third_party
  PROPERTIES
    OUTPUT_NAME z7_third_party)

if(DEFINED Z7_SHARED_LIBRARY_OUTPUT_DIR)
  set_target_properties(z7_third_party
    PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${Z7_SHARED_LIBRARY_OUTPUT_DIR}")
endif()

if(WIN32)
  if(DEFINED Z7_EXECUTABLE_OUTPUT_DIR)
    set_target_properties(z7_third_party
      PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${Z7_EXECUTABLE_OUTPUT_DIR}")
  endif()
  if(DEFINED Z7_SHARED_LIBRARY_OUTPUT_DIR)
    set_target_properties(z7_third_party
      PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${Z7_SHARED_LIBRARY_OUTPUT_DIR}")
  endif()
elseif(APPLE)
  set_target_properties(z7_third_party
    PROPERTIES
      BUILD_RPATH "@loader_path"
      INSTALL_RPATH "@loader_path")
else()
  set_target_properties(z7_third_party
    PROPERTIES
      BUILD_RPATH "$ORIGIN"
      INSTALL_RPATH "$ORIGIN")
endif()

if(WIN32)
  set_target_properties(z7_third_party
    PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
z7_set_default_target_options(z7_third_party)

if(Z7_ENABLE_INSTALL)
  install(TARGETS z7_third_party
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
