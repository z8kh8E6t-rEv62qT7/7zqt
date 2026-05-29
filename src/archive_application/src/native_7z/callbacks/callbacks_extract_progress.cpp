// src/archive_application/src/native_7z/callbacks/callbacks_extract_progress.cpp
// Role: Extract callback progress snapshot emission helpers.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

NativeExtractCallback::ProgressSnapshot
NativeExtractCallback::snapshot_progress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  ProgressSnapshot snap;
  snap.totals_known = totals_known_;
  snap.total_bytes = total_bytes_;
  snap.completed_bytes = completed_bytes_;
  snap.total_files = total_files_;
  snap.completed_files = completed_files_;
  snap.error_count = error_count_;
  snap.current_path = current_path_;
  if (ratio_input_size_known_ || ratio_output_size_known_) {
    ProgressRatioInfo ratio;
    ratio.input_size_known = ratio_input_size_known_;
    ratio.input_size = ratio_input_size_;
    ratio.output_size_known = ratio_output_size_known_;
    ratio.output_size = ratio_output_size_;
    ratio.compressing_mode = false;
    snap.ratio_info = ratio;
  }
  return snap;
}

void NativeExtractCallback::emit_progress_snapshot() const {
  const ProgressSnapshot snap = snapshot_progress();

  int percent = -1;
  if (snap.totals_known && snap.total_bytes != 0) {
    percent = static_cast<int>((snap.completed_bytes * 100) / snap.total_bytes);
  } else if (snap.total_files != 0) {
    percent = static_cast<int>((snap.completed_files * 100) / snap.total_files);
  }

  emit_progress_event(hooks_,
                      OperationStage::kRunning,
                      percent,
                      snap.totals_known,
                      snap.total_bytes,
                      snap.completed_bytes,
                      snap.total_files,
                      snap.completed_files,
                      snap.error_count,
                      snap.current_path,
                      {},
                      snap.ratio_info);
}

}  // namespace z7::app
