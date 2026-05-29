// src/archive_application/src/native_7z/third_party_adapter/callback_base.h
// Role: Shared pause/cancel checks for native callback implementations.

#pragma once

#include <atomic>
#include <functional>

namespace z7::app {

class CallbackBase {
 protected:
  CallbackBase(std::atomic<bool>* cancel_requested,
               std::function<bool()> wait_while_paused)
      : cancel_requested_(cancel_requested),
        wait_while_paused_(std::move(wait_while_paused)) {}

  bool should_abort() const {
    if (wait_while_paused_ && !wait_while_paused_()) {
      return true;
    }
    return cancel_requested_ != nullptr && cancel_requested_->load();
  }

 private:
  std::atomic<bool>* cancel_requested_ = nullptr;
  std::function<bool()> wait_while_paused_;
};

}  // namespace z7::app
