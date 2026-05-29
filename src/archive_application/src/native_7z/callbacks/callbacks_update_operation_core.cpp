// src/archive_application/src/native_7z/callbacks/callbacks_update_operation_core.cpp
// Role: Core state access and progress snapshot handling for update callbacks.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {

NativeUpdateOperationCallback::NativeUpdateOperationCallback(
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    std::string archive_path,
    Mode mode,
    std::string initial_password)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      hooks_(hooks),
      archive_path_(std::move(archive_path)),
      mode_(mode),
      password_(std::move(initial_password)) {}

bool NativeUpdateOperationCallback::totals_known() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return totals_known_;
}

uint64_t NativeUpdateOperationCallback::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_bytes_;
}

uint64_t NativeUpdateOperationCallback::completed_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_bytes_;
}

uint64_t NativeUpdateOperationCallback::total_files() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_files_;
}

uint64_t NativeUpdateOperationCallback::completed_files() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_files_;
}

uint64_t NativeUpdateOperationCallback::error_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_count_;
}

std::string NativeUpdateOperationCallback::current_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_path_;
}

std::optional<ProgressRatioInfo> NativeUpdateOperationCallback::ratio_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ratio_input_size_known_ && !ratio_output_size_known_) {
    return std::nullopt;
  }
  ProgressRatioInfo ratio;
  ratio.input_size_known = ratio_input_size_known_;
  ratio.input_size = ratio_input_size_;
  ratio.output_size_known = ratio_output_size_known_;
  ratio.output_size = ratio_output_size_;
  ratio.compressing_mode = true;
  return ratio;
}

std::string NativeUpdateOperationCallback::password() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return password_;
}

bool NativeUpdateOperationCallback::password_requested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return password_requested_;
}

bool NativeUpdateOperationCallback::wrong_password() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wrong_password_;
}

void NativeUpdateOperationCallback::set_total_files_hint(uint64_t total_files) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (total_files_ == 0) {
      total_files_ = total_files;
    }
  }
  emit_progress_snapshot();
}

NativeUpdateOperationCallback::ProgressSnapshot
NativeUpdateOperationCallback::snapshot_progress() const {
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
    ratio.compressing_mode = true;
    snap.ratio_info = ratio;
  }
  return snap;
}

void NativeUpdateOperationCallback::emit_progress_snapshot() const {
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

void NativeUpdateOperationCallback::note_error(const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++error_count_;
  }
  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kStdErr,
                 message);
  emit_progress_snapshot();
}

HRESULT NativeUpdateOperationCallback::check_break() const {
  return should_abort() ? E_ABORT : S_OK;
}

}  // namespace z7::app
