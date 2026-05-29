add_library(z7_i18n STATIC
  include/json_localization.h
  src/json_localization_internal.h
  src/json_localization.cpp
  src/resources/z7_i18n_resources.qrc
)

add_library(z7::i18n ALIAS z7_i18n)

target_include_directories(z7_i18n
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_definitions(z7_i18n
  PRIVATE
    Z7_I18N_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")

target_link_libraries(z7_i18n
  PUBLIC
    Qt6::Core)

z7_set_default_target_options(z7_i18n)
