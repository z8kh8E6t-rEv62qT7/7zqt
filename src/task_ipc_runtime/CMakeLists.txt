add_library(z7_task_ipc_runtime STATIC
  include/task_ipc_runtime.h
  src/core/internal.h
  src/core/constants.cpp
  src/core/notifiers.cpp
  src/core/posix.cpp
  src/core/shared_memory.cpp
  src/core/slots.cpp
  src/task_ipc_runtime_internal.h
  src/task_ipc_runtime_bootstrap.cpp
  src/task_ipc_runtime_payload.cpp
  src/task_ipc_runtime_dispatch_claim.cpp
  src/task_ipc_runtime_completion.cpp
)

add_library(z7::task_ipc_runtime ALIAS z7_task_ipc_runtime)

target_include_directories(z7_task_ipc_runtime
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(z7_task_ipc_runtime
  PUBLIC
    Qt6::Core
  PRIVATE
    z7_platform_qt)

z7_set_default_target_options(z7_task_ipc_runtime)
