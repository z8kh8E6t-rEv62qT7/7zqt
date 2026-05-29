#include "internal.h"

#include <algorithm>

namespace capi = z7::macos_integration::capi_internal;

namespace {

bool dispatch_prompt_entry(const std::shared_ptr<PromptDispatchEntry>& entry) {
  if (!entry) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(entry->mutex);
    if (entry->canceled || entry->dispatched) {
      return false;
    }
    entry->dispatched = true;
    entry->canceled = false;
  }
  entry->cv.notify_all();
  return true;
}

}  // namespace

extern "C" {

void z7_mi_session_set_password_prompt_callback(
    z7_mi_session_t* session,
    z7_mi_password_prompt_callback_t callback,
    void* user_data) {
  if (session == nullptr || !session->state) {
    return;
  }
  std::lock_guard<std::mutex> lock(session->state->mutex);
  session->state->password_prompt_callback = callback;
  session->state->password_prompt_user_data = user_data;
}

void z7_mi_password_prompt_provide(z7_mi_password_prompt_t* handle,
                                   const char* password) {
  if (handle == nullptr) {
    return;
  }
  if (password == nullptr) {
    capi::finish_prompt_slot(handle->slot, true);
    if (const auto state = handle->state.lock()) {
      capi::advance_password_prompt_dispatch(state, handle->dispatch);
    }
    delete handle;
    return;
  }
  capi::finish_prompt_slot(handle->slot, false, std::string(password));
  if (const auto state = handle->state.lock()) {
    capi::advance_password_prompt_dispatch(state, handle->dispatch);
  }
  delete handle;
}

void z7_mi_password_prompt_cancel(z7_mi_password_prompt_t* handle) {
  if (handle == nullptr) {
    return;
  }
  capi::finish_prompt_slot(handle->slot, true);
  if (const auto state = handle->state.lock()) {
    capi::advance_password_prompt_dispatch(state, handle->dispatch);
  }
  delete handle;
}

}  // extern "C"

namespace z7::macos_integration::capi_internal {

void finish_prompt_slot(const std::shared_ptr<PromptSlot>& slot,
                        bool canceled,
                        std::string password) {
  if (!slot) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(slot->mutex);
    if (slot->done) {
      return;
    }
    slot->canceled = canceled;
    slot->password = std::move(password);
    slot->done = true;
  }
  slot->cv.notify_all();
}

void cancel_all_pending_prompts(const std::shared_ptr<z7_mi_session_state>& state) {
  if (!state) {
    return;
  }

  std::vector<std::shared_ptr<PromptSlot>> prompt_slots;
  std::vector<std::shared_ptr<PromptDispatchEntry>> queued_dispatches;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    prompt_slots.reserve(static_cast<size_t>(state->pending_prompts.size()) +
                         state->orphan_prompt_slots.size());
    for (auto it = state->pending_prompts.begin(); it != state->pending_prompts.end(); ++it) {
      if (it.value()) {
        prompt_slots.push_back(it.value());
      }
    }
    state->pending_prompts.clear();
    for (const auto& weak_slot : state->orphan_prompt_slots) {
      if (const auto slot = weak_slot.lock()) {
        prompt_slots.push_back(slot);
      }
    }
    state->orphan_prompt_slots.clear();
    if (state->active_prompt_dispatch) {
      queued_dispatches.push_back(state->active_prompt_dispatch);
      state->active_prompt_dispatch.reset();
    }
    while (!state->queued_prompt_dispatches.empty()) {
      queued_dispatches.push_back(state->queued_prompt_dispatches.front());
      state->queued_prompt_dispatches.pop_front();
    }
  }

  for (const auto& dispatch : queued_dispatches) {
    if (!dispatch) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(dispatch->mutex);
      dispatch->canceled = true;
    }
    dispatch->cv.notify_all();
  }

  for (const auto& slot : prompt_slots) {
    finish_prompt_slot(slot, true);
  }
}

void clear_password_cache(const std::shared_ptr<z7_mi_session_state>& state) {
  if (!state) {
    return;
  }

  std::lock_guard<std::mutex> lock(state->mutex);
  for (auto it = state->password_cache.begin(); it != state->password_cache.end(); ++it) {
    std::string& password = it.value();
    std::fill(password.begin(), password.end(), '\0');
    password.clear();
  }
  state->password_cache.clear();
  state->password_prompt_callback = nullptr;
  state->password_prompt_user_data = nullptr;
}

void queue_password_prompt_dispatch(
    const std::shared_ptr<z7_mi_session_state>& state,
    const std::shared_ptr<PromptDispatchEntry>& dispatch) {
  if (!state || !dispatch) {
    return;
  }

  bool dispatch_now = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->active_prompt_dispatch) {
      state->active_prompt_dispatch = dispatch;
      dispatch_now = true;
    } else {
      state->queued_prompt_dispatches.push_back(dispatch);
    }
  }

  if (dispatch_now) {
    dispatch_prompt_entry(dispatch);
  }
}

void advance_password_prompt_dispatch(
    const std::shared_ptr<z7_mi_session_state>& state,
    const std::shared_ptr<PromptDispatchEntry>& completed_dispatch) {
  if (!state || !completed_dispatch) {
    return;
  }

  auto current_dispatch = completed_dispatch;
  while (current_dispatch) {
    std::shared_ptr<PromptDispatchEntry> next_dispatch;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->active_prompt_dispatch == current_dispatch) {
        state->active_prompt_dispatch.reset();
        while (!state->queued_prompt_dispatches.empty()) {
          const auto candidate = state->queued_prompt_dispatches.front();
          state->queued_prompt_dispatches.pop_front();
          if (!candidate) {
            continue;
          }
          std::lock_guard<std::mutex> candidate_lock(candidate->mutex);
          if (candidate->canceled) {
            continue;
          }
          next_dispatch = candidate;
          state->active_prompt_dispatch = candidate;
          break;
        }
      } else {
        auto it = std::find(state->queued_prompt_dispatches.begin(),
                            state->queued_prompt_dispatches.end(),
                            current_dispatch);
        if (it != state->queued_prompt_dispatches.end()) {
          state->queued_prompt_dispatches.erase(it);
        }
        return;
      }
    }

    if (!next_dispatch) {
      return;
    }
    if (dispatch_prompt_entry(next_dispatch)) {
      return;
    }
    current_dispatch = next_dispatch;
  }
}

}  // namespace z7::macos_integration::capi_internal
