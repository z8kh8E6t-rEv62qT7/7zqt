enable_language(OBJCXX)

find_library(Z7_APPKIT_FRAMEWORK
  AppKit
  REQUIRED)

add_library(z7_macos_integration_launch_services STATIC
  ../include/macos_launch_services.h
  macos_launch_services.mm
)

add_library(z7::macos_integration_launch_services ALIAS z7_macos_integration_launch_services)

target_include_directories(z7_macos_integration_launch_services
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../include")

target_link_libraries(z7_macos_integration_launch_services
  PUBLIC
    Qt6::Core
  PRIVATE
    "${Z7_APPKIT_FRAMEWORK}")

z7_set_default_target_options(z7_macos_integration_launch_services)
