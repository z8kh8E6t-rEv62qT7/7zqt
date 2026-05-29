#include "internal.h"

namespace capi = z7::macos_integration::capi_internal;

extern "C" {

void z7_mi_task_cancel(z7_mi_task_t* task) {
  if (task == nullptr) {
    return;
  }
  const auto state = task->state.lock();
  if (!state || task->task_id == 0) {
    return;
  }
  const auto task_state = capi::lookup_in_flight_task(state, task->task_id);
  if (!task_state) {
    return;
  }
  task_state->cancel_requested.store(true);
  capi::cancel_all_pending_prompts(state);
  capi::cancel_active_archive_session(task_state);
}

void z7_mi_task_release(z7_mi_task_t* task) {
  delete task;
}

}  // extern "C"
