// src/archive_application/src/native_7z/callbacks/callbacks_update_operation_flow.cpp
// Role: Update/delete operation callbacks for item processing and progress mutation.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {

HRESULT NativeUpdateOperationCallback::StartArchive(const wchar_t* name, bool) {
  const std::string path = update_wide_name_to_utf8(name);
  if (!path.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    archive_path_ = path;
  }
  if (!path.empty()) {
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Updating " + path);
  }
  return check_break();
}

HRESULT NativeUpdateOperationCallback::FinishArchive(const CFinishArchiveStat&) {
  return check_break();
}

HRESULT NativeUpdateOperationCallback::DeletingAfterArchiving(const FString& path,
                                                              bool) {
  const std::string value = ustring_to_utf8(fs2us(path));
  if (!value.empty()) {
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Deleting " + value);
  }
  return check_break();
}

HRESULT NativeUpdateOperationCallback::FinishDeletingAfterArchiving() {
  return check_break();
}

HRESULT NativeUpdateOperationCallback::MoveArc_Start(const wchar_t*,
                                                     const wchar_t*,
                                                     UInt64 size,
                                                     Int32) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_known_ = true;
    total_bytes_ = size;
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::MoveArc_Progress(UInt64 total,
                                                        UInt64 current) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_known_ = true;
    total_bytes_ = total;
    completed_bytes_ = current;
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::MoveArc_Finish() {
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::WriteSfx(const wchar_t*, UInt64) {
  return check_break();
}

HRESULT NativeUpdateOperationCallback::SetTotal(UInt64 size) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_known_ = true;
    total_bytes_ = size;
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::SetCompleted(const UInt64* complete_value) {
  if (complete_value != nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_bytes_ = *complete_value;
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::SetRatioInfo(const UInt64* in_size,
                                                    const UInt64* out_size) {
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
  return check_break();
}

HRESULT NativeUpdateOperationCallback::CheckBreak() {
  return check_break();
}

HRESULT NativeUpdateOperationCallback::SetNumItems(const CArcToDoStat& stat) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    total_files_ = stat.Get_NumDataItems_Total();
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::GetStream(const wchar_t* name,
                                                 bool,
                                                 bool is_anti,
                                                 UInt32) {
  const std::string path = update_wide_name_to_utf8(name);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_path_ = path;
  }
  if (!path.empty()) {
    std::string op;
    if (mode_ == Mode::kDelete || is_anti) {
      op = "Deleting ";
    } else {
      op = "Adding ";
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   op + path);
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::OpenFileError(const FString& path, DWORD) {
  const std::string value = ustring_to_utf8(fs2us(path));
  note_error(value.empty() ? "Open file error" : ("Open file error: " + value));
  return S_FALSE;
}

HRESULT NativeUpdateOperationCallback::ReadingFileError(const FString& path,
                                                        DWORD) {
  const std::string value = ustring_to_utf8(fs2us(path));
  note_error(value.empty() ? "Read file error" : ("Read file error: " + value));
  return S_FALSE;
}

HRESULT NativeUpdateOperationCallback::SetOperationResult(Int32 op_res) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++completed_files_;
    if (op_res != NArchive::NUpdate::NOperationResult::kOK) {
      ++error_count_;
    }
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::ReportExtractResult(Int32 op_res,
                                                           Int32,
                                                           const wchar_t* name) {
  if (op_res == NArchive::NExtract::NOperationResult::kWrongPassword) {
    std::lock_guard<std::mutex> lock(mutex_);
    wrong_password_ = true;
  }
  if (op_res != NArchive::NExtract::NOperationResult::kOK) {
    const std::string path = update_wide_name_to_utf8(name);
    note_error(path.empty()
                   ? "Update extract error"
                   : (path + " : " + test_operation_result_message(op_res)));
  }
  return check_break();
}

HRESULT NativeUpdateOperationCallback::ReportUpdateOperation(UInt32,
                                                             const wchar_t* name,
                                                             bool) {
  const std::string path = update_wide_name_to_utf8(name);
  if (!path.empty()) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_path_ = path;
    }
    emit_progress_snapshot();
  }
  return check_break();
}

HRESULT NativeUpdateOperationCallback::ShowDeleteFile(const wchar_t* name, bool) {
  const std::string path = update_wide_name_to_utf8(name);
  if (!path.empty()) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_path_ = path;
      if (mode_ == Mode::kDelete) {
        ++completed_files_;
      }
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Deleting " + path);
    emit_progress_snapshot();
  }
  return check_break();
}

}  // namespace z7::app
