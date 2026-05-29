if(NOT DEFINED SOURCE_LANG_DIR)
  message(FATAL_ERROR "SOURCE_LANG_DIR is required")
endif()
if(NOT DEFINED SOURCE_FM_DIR)
  message(FATAL_ERROR "SOURCE_FM_DIR is required")
endif()
if(NOT DEFINED SOURCE_ARCHIVE_ICON_DIR)
  message(FATAL_ERROR "SOURCE_ARCHIVE_ICON_DIR is required")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "OUT_DIR is required")
endif()
if(NOT DEFINED OUT_QRC)
  message(FATAL_ERROR "OUT_QRC is required")
endif()

set(LANG_OUTPUT_DIR "${OUT_DIR}/lang")
set(ICON_OUTPUT_DIR "${OUT_DIR}/icons")
set(ARCHIVE_ICON_OUTPUT_DIR "${OUT_DIR}/archive-icons")
set(FM_RC_OUTPUT_DIR "${OUT_DIR}/fm-rc")
file(MAKE_DIRECTORY "${LANG_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${ICON_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${ARCHIVE_ICON_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FM_RC_OUTPUT_DIR}")

file(GLOB LANG_SOURCE_FILES
  LIST_DIRECTORIES FALSE
  "${SOURCE_LANG_DIR}/*.txt"
  "${SOURCE_LANG_DIR}/en.ttt")
list(SORT LANG_SOURCE_FILES)

if(LANG_SOURCE_FILES STREQUAL "")
  message(FATAL_ERROR "No language files found in: ${SOURCE_LANG_DIR}")
endif()

foreach(LANG_SOURCE_FILE IN LISTS LANG_SOURCE_FILES)
  get_filename_component(LANG_FILE_NAME "${LANG_SOURCE_FILE}" NAME)
  configure_file(
    "${LANG_SOURCE_FILE}"
    "${LANG_OUTPUT_DIR}/${LANG_FILE_NAME}"
    COPYONLY)
endforeach()

set(ICON_BASENAMES
  "Add.bmp"
  "Extract.bmp"
  "Test.bmp"
  "Copy.bmp"
  "Move.bmp"
  "Delete.bmp"
  "Info.bmp"
  "Add2.bmp"
  "Extract2.bmp"
  "Test2.bmp"
  "Copy2.bmp"
  "Move2.bmp"
  "Delete2.bmp"
  "Info2.bmp"
  "FM.ico")

set(COPIED_ICON_BASENAMES "")
foreach(ICON_BASENAME IN LISTS ICON_BASENAMES)
  set(ICON_SOURCE_FILE "${SOURCE_FM_DIR}/${ICON_BASENAME}")
  if(EXISTS "${ICON_SOURCE_FILE}")
    configure_file(
      "${ICON_SOURCE_FILE}"
      "${ICON_OUTPUT_DIR}/${ICON_BASENAME}"
      COPYONLY)
    list(APPEND COPIED_ICON_BASENAMES "${ICON_BASENAME}")
  endif()
endforeach()

file(GLOB ARCHIVE_ICON_SOURCE_FILES
  LIST_DIRECTORIES FALSE
  "${SOURCE_ARCHIVE_ICON_DIR}/*.ico")
list(SORT ARCHIVE_ICON_SOURCE_FILES)

if(ARCHIVE_ICON_SOURCE_FILES STREQUAL "")
  message(FATAL_ERROR "No archive icon files found in: ${SOURCE_ARCHIVE_ICON_DIR}")
endif()

set(COPIED_ARCHIVE_ICON_BASENAMES "")
foreach(ARCHIVE_ICON_SOURCE_FILE IN LISTS ARCHIVE_ICON_SOURCE_FILES)
  get_filename_component(ARCHIVE_ICON_BASENAME "${ARCHIVE_ICON_SOURCE_FILE}" NAME)
  configure_file(
    "${ARCHIVE_ICON_SOURCE_FILE}"
    "${ARCHIVE_ICON_OUTPUT_DIR}/${ARCHIVE_ICON_BASENAME}"
    COPYONLY)
  list(APPEND COPIED_ARCHIVE_ICON_BASENAMES "${ARCHIVE_ICON_BASENAME}")
endforeach()

set(FM_RC_BASENAMES
  "PropertyName.rc"
  "resource.rc"
  "resourceGui.rc"
  "PropertyNameRes.h"
  "resource.h"
  "resourceGui.h")

set(COPIED_FM_RC_BASENAMES "")
foreach(FM_RC_BASENAME IN LISTS FM_RC_BASENAMES)
  set(FM_RC_SOURCE_FILE "${SOURCE_FM_DIR}/${FM_RC_BASENAME}")
  if(EXISTS "${FM_RC_SOURCE_FILE}")
    configure_file(
      "${FM_RC_SOURCE_FILE}"
      "${FM_RC_OUTPUT_DIR}/${FM_RC_BASENAME}"
      COPYONLY)
    list(APPEND COPIED_FM_RC_BASENAMES "${FM_RC_BASENAME}")
  endif()
endforeach()

set(QRC_TEXT "<RCC>\n")
string(APPEND QRC_TEXT "  <qresource prefix=\"/z7/lang\">\n")
foreach(LANG_SOURCE_FILE IN LISTS LANG_SOURCE_FILES)
  get_filename_component(LANG_FILE_NAME "${LANG_SOURCE_FILE}" NAME)
  set(LANG_OUTPUT_FILE "${LANG_OUTPUT_DIR}/${LANG_FILE_NAME}")
  file(TO_CMAKE_PATH "${LANG_OUTPUT_FILE}" LANG_OUTPUT_FILE)
  string(APPEND QRC_TEXT
    "    <file alias=\"${LANG_FILE_NAME}\">${LANG_OUTPUT_FILE}</file>\n")
endforeach()
string(APPEND QRC_TEXT "  </qresource>\n")

string(APPEND QRC_TEXT "  <qresource prefix=\"/z7/fm-icons\">\n")
foreach(ICON_BASENAME IN LISTS COPIED_ICON_BASENAMES)
  set(ICON_OUTPUT_FILE "${ICON_OUTPUT_DIR}/${ICON_BASENAME}")
  file(TO_CMAKE_PATH "${ICON_OUTPUT_FILE}" ICON_OUTPUT_FILE)
  string(APPEND QRC_TEXT
    "    <file alias=\"${ICON_BASENAME}\">${ICON_OUTPUT_FILE}</file>\n")
endforeach()
string(APPEND QRC_TEXT "  </qresource>\n")

string(APPEND QRC_TEXT "  <qresource prefix=\"/z7/archive-icons\">\n")
foreach(ARCHIVE_ICON_BASENAME IN LISTS COPIED_ARCHIVE_ICON_BASENAMES)
  set(ARCHIVE_ICON_OUTPUT_FILE "${ARCHIVE_ICON_OUTPUT_DIR}/${ARCHIVE_ICON_BASENAME}")
  file(TO_CMAKE_PATH "${ARCHIVE_ICON_OUTPUT_FILE}" ARCHIVE_ICON_OUTPUT_FILE)
  string(APPEND QRC_TEXT
    "    <file alias=\"${ARCHIVE_ICON_BASENAME}\">${ARCHIVE_ICON_OUTPUT_FILE}</file>\n")
endforeach()
string(APPEND QRC_TEXT "  </qresource>\n")

string(APPEND QRC_TEXT "  <qresource prefix=\"/z7/fm-rc\">\n")
foreach(FM_RC_BASENAME IN LISTS COPIED_FM_RC_BASENAMES)
  set(FM_RC_OUTPUT_FILE "${FM_RC_OUTPUT_DIR}/${FM_RC_BASENAME}")
  file(TO_CMAKE_PATH "${FM_RC_OUTPUT_FILE}" FM_RC_OUTPUT_FILE)
  string(APPEND QRC_TEXT
    "    <file alias=\"${FM_RC_BASENAME}\">${FM_RC_OUTPUT_FILE}</file>\n")
endforeach()
string(APPEND QRC_TEXT "  </qresource>\n")

string(APPEND QRC_TEXT "</RCC>\n")

file(WRITE "${OUT_QRC}" "${QRC_TEXT}")
