add_library(z7_common INTERFACE)

add_library(z7::common ALIAS z7_common)

target_include_directories(z7_common
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features(z7_common
  INTERFACE
    cxx_std_20)
