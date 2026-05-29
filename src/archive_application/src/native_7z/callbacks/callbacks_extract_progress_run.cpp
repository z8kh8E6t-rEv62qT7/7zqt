// src/archive_application/src/native_7z/callbacks/callbacks_extract_progress_run.cpp
// Role: Extract callback 7-Zip progress and prepare-operation methods.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

STDMETHODIMP NativeExtractCallback::SetTotal(UInt64 total) throw() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_known_ = true;
    total_bytes_ = total;
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeExtractCallback::SetCompleted(
    const UInt64* complete_value) throw() {
  if (complete_value != nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_bytes_ = *complete_value;
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeExtractCallback::SetRatioInfo(const UInt64* in_size,
                                                 const UInt64* out_size) throw() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (in_size != nullptr) {
      ratio_input_size_known_ = true;
      ratio_input_size_ = *in_size;
    }
    if (out_size != nullptr) {
      ratio_output_size_known_ = true;
      ratio_output_size_ = *out_size;
    }
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeExtractCallback::PrepareOperation(Int32 ask_extract_mode) throw() {
  if (ask_extract_mode == NArchive::NExtract::NAskMode::kExtract) {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      path = current_path_;
    }
    if (!path.empty()) {
      emit_log_event(hooks_,
                     OperationStage::kRunning,
                     OutputChannel::kNone,
                     "Extracting " + path);
    }
  }
  emit_progress_snapshot();
  return check_canceled();
}

}  // namespace z7::app
