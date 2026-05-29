// src/archive_application/src/native_7z/info/info_runtime.cpp
// Role: Cancellation/pause runtime controls.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

void NativeArchiveBackend::cancel() {
    cancel_requested_.store(true);
    pause_requested_.store(false);
    pause_cv_.notify_all();
  }

bool NativeArchiveBackend::supports_pause() const {
    return hashing_active_.load() ||
           testing_active_.load() ||
           extracting_active_.load() ||
           updating_active_.load() ||
           benchmarking_active_.load();
  }

void NativeArchiveBackend::pause() {
    if (!supports_pause()) {
      return;
    }
    pause_requested_.store(true);
  }

void NativeArchiveBackend::resume() {
    pause_requested_.store(false);
    pause_cv_.notify_all();
  }

bool NativeArchiveBackend::wait_while_paused() {
    if (!pause_requested_.load()) {
      return !cancel_requested_.load();
    }

    std::unique_lock<std::mutex> lock(pause_mutex_);
    pause_cv_.wait(lock, [this]() {
      return cancel_requested_.load() || !pause_requested_.load();
    });
    return !cancel_requested_.load();
  }


}  // namespace z7::app
