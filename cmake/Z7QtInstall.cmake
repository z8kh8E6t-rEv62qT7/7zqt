include_guard(GLOBAL)

include(GNUInstallDirs)

function(z7_install_base)
  if(NOT Z7_ENABLE_INSTALL)
    return()
  endif()

  install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/z7qt"
    OPTIONAL)
endfunction()

function(z7_install_linux_desktop_entries)
  if(NOT Z7_ENABLE_INSTALL)
    return()
  endif()

  if(UNIX AND NOT APPLE)
    install(FILES
      "${PROJECT_SOURCE_DIR}/packaging/linux/7zfm.desktop"
      "${PROJECT_SOURCE_DIR}/packaging/linux/7zg.desktop"
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
  endif()
endfunction()
