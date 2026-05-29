cmake_minimum_required(VERSION 3.31)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are disabled.\n"
    "Use the preset workflow instead:\n"
    "  cmake --preset dev\n"
    "  cmake --build --preset dev --target 7zFM")
endif()

if(APPLE)
  if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER)
    set(_z7_hb_llvm_roots
      "/opt/homebrew/opt/llvm"
      "/usr/local/opt/llvm")

    foreach(_z7_llvm_root IN LISTS _z7_hb_llvm_roots)
      if(EXISTS "${_z7_llvm_root}/bin/clang" AND
         EXISTS "${_z7_llvm_root}/bin/clang++")
        set(CMAKE_C_COMPILER "${_z7_llvm_root}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${_z7_llvm_root}/bin/clang++" CACHE FILEPATH "C++ compiler" FORCE)
        if(EXISTS "${_z7_llvm_root}/bin/llvm-ar")
          set(CMAKE_AR "${_z7_llvm_root}/bin/llvm-ar" CACHE FILEPATH "Archiver" FORCE)
        endif()
        if(EXISTS "${_z7_llvm_root}/bin/llvm-ranlib")
          set(CMAKE_RANLIB "${_z7_llvm_root}/bin/llvm-ranlib" CACHE FILEPATH "Ranlib" FORCE)
        endif()
        set(Z7_PREFERRED_HOMEBREW_LLVM_ROOT "${_z7_llvm_root}" CACHE INTERNAL
            "Preferred Homebrew LLVM root selected during CMake configure.")
        message(STATUS "z7qt: using Homebrew LLVM toolchain from ${_z7_llvm_root}")
        break()
      endif()
    endforeach()
  endif()
endif()

if(APPLE)
  set(Z7_MACOS_DEPLOYMENT_TARGET "15.0" CACHE STRING
      "Default macOS deployment target for all targets in this project.")
  if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${Z7_MACOS_DEPLOYMENT_TARGET}" CACHE STRING
        "Minimum macOS deployment target for generated build rules." FORCE)
  endif()
  set(Z7_MACOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}" CACHE STRING
      "Default macOS deployment target for all targets in this project." FORCE)
  message(STATUS "z7qt: macOS deployment target = ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

project(z7qt LANGUAGES C CXX)

set(Z7_EXECUTABLE_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin")
set(Z7_SHARED_LIBRARY_OUTPUT_DIR "${PROJECT_BINARY_DIR}/lib")

include("${CMAKE_CURRENT_SOURCE_DIR}/BuildOptions.cmake")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(Z7QtOptions)
include(Z7LlvmCoverage)
include(Z7QtTargets)
include(Z7QtLangResources)
include(Z7BoundaryChecks)
include(Z7UnusedAnalysis)

z7_configure_llvm_coverage()

if(Z7_QT_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${Z7_QT_ROOT}")
endif()

find_package(Qt6 ${Z7_QT_VERSION_MIN} REQUIRED COMPONENTS Core Gui Widgets Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(src)
include(Z7QtDeployMacOS)

if(Z7_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

z7_add_llvm_coverage_targets()

include(Z7QtInstall)
if(Z7_ENABLE_INSTALL)
  z7_install_base()
  z7_install_linux_desktop_entries()
endif()
