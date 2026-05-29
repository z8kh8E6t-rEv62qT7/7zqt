#include "internal.h"

namespace capi = z7::macos_integration::capi_internal;

extern "C" {

z7_mi_session_t* z7_mi_session_create(void) {
  capi::ensure_qt_core_app();
  auto* session = new z7_mi_session_t();
  session->state = std::make_shared<z7_mi_session_state>();
  return session;
}

void z7_mi_session_destroy(z7_mi_session_t* session) {
  if (session == nullptr) {
    return;
  }
  if (const auto& state = session->state) {
    std::vector<std::shared_ptr<AsyncTaskState>> tasks;
    capi::cancel_all_pending_prompts(state);
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      tasks.reserve(state->in_flight_tasks.size());
      for (const auto& pair : state->in_flight_tasks) {
        tasks.push_back(pair.second);
      }
    }
    for (const auto& task : tasks) {
      if (!task || task->completed.load()) {
        continue;
      }
      task->cancel_requested.store(true);
      capi::cancel_active_archive_session(task);
    }
    for (const auto& task : tasks) {
      capi::wait_for_task_completion(task);
    }
    const auto cached_chains = capi::take_nested_session_cache_for_destroy(state);
    for (const auto& chain : cached_chains) {
      capi::close_nested_session_chain(chain);
    }
    capi::clear_password_cache(state);
  }
  delete session;
}

}  // extern "C"
