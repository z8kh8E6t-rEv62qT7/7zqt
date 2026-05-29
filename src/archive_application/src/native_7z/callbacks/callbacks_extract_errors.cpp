// src/archive_application/src/native_7z/callbacks/callbacks_extract_errors.cpp
// Role: Extract callback error recording and cancellation helpers.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

void NativeExtractCallback::record_io_error(const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    io_error_ = true;
    if (io_error_message_.empty()) {
      io_error_message_ = message;
    }
    if (!diagnostic_message_.empty()) {
      diagnostic_message_ += '\n';
    }
    diagnostic_message_ += message;
  }
  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kStdErr,
                 message);
}

HRESULT NativeExtractCallback::check_canceled() const {
  if (budget_triggered_.load(std::memory_order_acquire)) {
    return E_ABORT;
  }
  return should_abort() ? E_ABORT : S_OK;
}

}  // namespace z7::app
