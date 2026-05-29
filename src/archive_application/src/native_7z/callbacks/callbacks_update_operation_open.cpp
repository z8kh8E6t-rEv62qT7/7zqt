// src/archive_application/src/native_7z/callbacks/callbacks_update_operation_open.cpp
// Role: Archive-open and scan-stage callback handling for update operations.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {

HRESULT NativeUpdateOperationCallback::OpenResult(const CCodecs*,
                                                  const CArchiveLink&,
                                                  const wchar_t* name,
                                                  HRESULT result) {
  if (result != S_OK) {
    const std::string path = update_wide_name_to_utf8(name);
    std::string message = "Open archive failed";
    if (!path.empty()) {
      message += ": " + path;
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kStdErr,
                   message);
  }
  return S_OK;
}

HRESULT NativeUpdateOperationCallback::StartScanning() {
  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kNone,
                 "Scanning");
  return check_break();
}

HRESULT NativeUpdateOperationCallback::FinishScanning(const CDirItemsStat& st) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    total_files_ = st.NumFiles + st.NumAltStreams;
    if (st.GetTotalBytes() > 0) {
      totals_known_ = true;
      total_bytes_ = st.GetTotalBytes();
    }
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::StartOpenArchive(const wchar_t* name) {
  const std::string path = update_wide_name_to_utf8(name);
  if (!path.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    archive_path_ = path;
  }
  if (!path.empty()) {
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Opening " + path);
  }
  return check_break();
}

HRESULT NativeUpdateOperationCallback::ScanError(const FString& path, DWORD) {
  const std::string value = ustring_to_utf8(fs2us(path));
  note_error(value.empty() ? "Scan error" : ("Scan error: " + value));
  return S_FALSE;
}

HRESULT NativeUpdateOperationCallback::ScanProgress(const CDirItemsStat& st,
                                                    const FString& path,
                                                    bool) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    total_files_ = st.NumFiles + st.NumAltStreams;
    if (st.GetTotalBytes() > 0) {
      totals_known_ = true;
      total_bytes_ = st.GetTotalBytes();
    }
    current_path_ = ustring_to_utf8(fs2us(path));
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::Open_CheckBreak() {
  return check_break();
}

HRESULT NativeUpdateOperationCallback::Open_SetTotal(const UInt64* files,
                                                     const UInt64* bytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (files != nullptr) {
      total_files_ = *files;
    }
    if (bytes != nullptr) {
      totals_known_ = true;
      total_bytes_ = *bytes;
    }
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::Open_SetCompleted(const UInt64* files,
                                                         const UInt64* bytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (files != nullptr) {
      completed_files_ = *files;
    }
    if (bytes != nullptr) {
      completed_bytes_ = *bytes;
    }
  }
  emit_progress_snapshot();
  return check_break();
}

HRESULT NativeUpdateOperationCallback::Open_Finished() {
  return check_break();
}

#ifndef Z7_NO_CRYPTO
HRESULT NativeUpdateOperationCallback::provide_password(BSTR* password, bool force_prompt) {
  if (password == nullptr) {
    return E_INVALIDARG;
  }
  *password = nullptr;

  std::string password_value;
  std::string archive_path;
  bool wrong_password = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    password_value = password_;
    archive_path = archive_path_;
    wrong_password = wrong_password_;
  }

  if (wrong_password || force_prompt) {
    if (!hooks_.ask_password) {
      std::lock_guard<std::mutex> lock(mutex_);
      password_requested_ = true;
      wrong_password_ = wrong_password;
      return S_OK;
    }

    PasswordPrompt prompt;
    prompt.archive_path = archive_path;
    prompt.reason_kind = wrong_password
                             ? PasswordPromptReason::kWrongPassword
                             : PasswordPromptReason::kPasswordRequired;
    prompt.reason = wrong_password ? "wrong_password" : "password_required";
    const PasswordReply reply = hooks_.ask_password(prompt);
    if (reply.kind != PasswordReplyKind::kProvide) {
      std::lock_guard<std::mutex> lock(mutex_);
      password_requested_ = true;
      wrong_password_ = wrong_password;
      return S_OK;
    }

    password_value = reply.password;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      password_ = password_value;
      wrong_password_ = false;
    }
  }

  const UString pass = utf8_to_ustring(password_value);
  const HRESULT pass_res = StringToBstr(pass, password);
  if (pass_res != S_OK) {
    std::lock_guard<std::mutex> lock(mutex_);
    password_requested_ = true;
  }
  return pass_res;
}

HRESULT NativeUpdateOperationCallback::CryptoGetTextPassword2(
    Int32* password_is_defined,
    BSTR* password) {
  const HRESULT password_res = provide_password(password, false);
  if (password_is_defined != nullptr) {
    const bool has_password = password_res == S_OK && password != nullptr &&
                              *password != nullptr && ::SysStringLen(*password) > 0;
    *password_is_defined = BoolToInt(has_password);
  }
  return password_res;
}

HRESULT NativeUpdateOperationCallback::CryptoGetTextPassword(BSTR* password) {
  return CryptoGetTextPassword2(nullptr, password);
}

HRESULT NativeUpdateOperationCallback::Open_CryptoGetTextPassword(BSTR* password) {
  return provide_password(password, true);
}
#endif

}  // namespace z7::app
