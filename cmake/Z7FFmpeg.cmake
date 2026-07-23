include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)

set(_z7_ffmpeg_release_base
  "https://github.com/z8kh8E6t-rEv62qT7/mpv-macbuild/releases/latest/download")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _z7_ffmpeg_processor)
if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR
      "Archive image preview requires the macOS arm64 FFmpeg asset; "
      "CMAKE_OSX_ARCHITECTURES is '${CMAKE_OSX_ARCHITECTURES}'.")
  endif()
  if(NOT _z7_ffmpeg_processor MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR
      "Archive image preview supports only macOS arm64; detected "
      "CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}'.")
  endif()
  set(_z7_ffmpeg_asset "ffmpeg-lgpl-macos15-arm64.tar.xz")
else()
  message(FATAL_ERROR
    "Archive image preview currently provides an FFmpeg asset only for "
    "macOS arm64; detected ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}.")
endif()

set(_z7_ffmpeg_cache_root "${CMAKE_BINARY_DIR}/_deps/z7_ffmpeg")
set(_z7_ffmpeg_archive "${_z7_ffmpeg_cache_root}/${_z7_ffmpeg_asset}")
set(_z7_ffmpeg_extract_root "${_z7_ffmpeg_cache_root}/extract")
set(_z7_ffmpeg_root "${_z7_ffmpeg_extract_root}/ffmpeg-lgpl")
set(_z7_ffmpeg_complete_marker "${_z7_ffmpeg_root}/.z7-download-complete")

if(NOT EXISTS "${_z7_ffmpeg_complete_marker}")
  file(REMOVE_RECURSE "${_z7_ffmpeg_extract_root}")
  file(MAKE_DIRECTORY "${_z7_ffmpeg_cache_root}")

  if(NOT EXISTS "${_z7_ffmpeg_archive}")
    set(_z7_ffmpeg_partial "${_z7_ffmpeg_archive}.part")
    file(REMOVE "${_z7_ffmpeg_partial}")
    message(STATUS "Downloading FFmpeg LGPL asset: ${_z7_ffmpeg_asset}")
    file(DOWNLOAD
      "${_z7_ffmpeg_release_base}/${_z7_ffmpeg_asset}"
      "${_z7_ffmpeg_partial}"
      STATUS _z7_ffmpeg_download_status
      LOG _z7_ffmpeg_download_log
      SHOW_PROGRESS
      TLS_VERIFY ON)
    list(GET _z7_ffmpeg_download_status 0 _z7_ffmpeg_download_code)
    list(GET _z7_ffmpeg_download_status 1 _z7_ffmpeg_download_message)
    if(NOT _z7_ffmpeg_download_code EQUAL 0)
      file(REMOVE "${_z7_ffmpeg_partial}")
      message(FATAL_ERROR
        "Failed to download ${_z7_ffmpeg_asset}: "
        "${_z7_ffmpeg_download_message}\n${_z7_ffmpeg_download_log}")
    endif()
    file(RENAME "${_z7_ffmpeg_partial}" "${_z7_ffmpeg_archive}")
  endif()

  file(MAKE_DIRECTORY "${_z7_ffmpeg_extract_root}")
  file(ARCHIVE_EXTRACT
    INPUT "${_z7_ffmpeg_archive}"
    DESTINATION "${_z7_ffmpeg_extract_root}")
  if(NOT EXISTS "${_z7_ffmpeg_root}/include/libavcodec/avcodec.h" OR
     NOT EXISTS "${_z7_ffmpeg_root}/lib/pkgconfig/libavformat.pc")
    file(REMOVE_RECURSE "${_z7_ffmpeg_extract_root}")
    message(FATAL_ERROR
      "FFmpeg asset ${_z7_ffmpeg_asset} does not contain the required "
      "ffmpeg-lgpl include/lib/pkgconfig layout.")
  endif()
  file(WRITE "${_z7_ffmpeg_complete_marker}" "${_z7_ffmpeg_asset}\n")
endif()

set(_z7_ffmpeg_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if(WIN32)
  set(_z7_ffmpeg_pkg_config_separator ";")
else()
  set(_z7_ffmpeg_pkg_config_separator ":")
endif()
if(_z7_ffmpeg_saved_pkg_config_path STREQUAL "")
  set(ENV{PKG_CONFIG_PATH} "${_z7_ffmpeg_root}/lib/pkgconfig")
else()
  set(ENV{PKG_CONFIG_PATH}
    "${_z7_ffmpeg_root}/lib/pkgconfig${_z7_ffmpeg_pkg_config_separator}${_z7_ffmpeg_saved_pkg_config_path}")
endif()

pkg_check_modules(Z7_FFMPEG REQUIRED IMPORTED_TARGET GLOBAL
  libavformat
  libavcodec
  libavutil
  libswscale)

set(ENV{PKG_CONFIG_PATH} "${_z7_ffmpeg_saved_pkg_config_path}")
set(Z7_FFMPEG_ROOT "${_z7_ffmpeg_root}" CACHE INTERNAL
  "Downloaded FFmpeg LGPL package root.")
set(Z7_FFMPEG_LIBRARY_DIR "${_z7_ffmpeg_root}/lib" CACHE INTERNAL
  "Downloaded FFmpeg LGPL library directory.")

message(STATUS "Using FFmpeg LGPL package from ${Z7_FFMPEG_ROOT}")
