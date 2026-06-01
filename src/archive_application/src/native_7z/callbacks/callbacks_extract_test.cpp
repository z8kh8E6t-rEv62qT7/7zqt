// src/archive_application/src/native_7z/callbacks/callbacks_extract_test.cpp
// Role: Test-mode extraction callback implementation.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_test.h"

namespace z7::app {

class NativeHashOutStream final : public ISequentialOutStream {
 public:
  explicit NativeHashOutStream(NativeTestExtractCallback* owner)
      : owner_(owner) {
    if (owner_ != nullptr) {
      owner_->AddRef();
    }
  }

  NativeHashOutStream(const NativeHashOutStream&) = delete;
  NativeHashOutStream& operator=(const NativeHashOutStream&) = delete;

  ~NativeHashOutStream() {
    if (owner_ != nullptr) {
      owner_->Release();
    }
  }

  STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override {
    if (out_object == nullptr) {
      return E_INVALIDARG;
    }
    *out_object = nullptr;
    if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
      *out_object = static_cast<ISequentialOutStream*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHOD_(ULONG, AddRef)() throw() override {
    return ++ref_count_;
  }

  STDMETHOD_(ULONG, Release)() throw() override {
    const ULONG next = --ref_count_;
    if (next == 0) {
      delete this;
    }
    return next;
  }

  STDMETHOD(Write)(const void* data,
                   UInt32 size,
                   UInt32* processed_size) throw() override {
    if (owner_ == nullptr) {
      if (processed_size != nullptr) {
        *processed_size = 0;
      }
      return E_FAIL;
    }
    return owner_->write_hash_data(data, size, processed_size);
  }

 private:
  std::atomic<ULONG> ref_count_{1};
  NativeTestExtractCallback* owner_ = nullptr;
};

NativeTestExtractCallback::NativeTestExtractCallback(
    IInArchive* archive,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    std::string archive_path,
    uint64_t total_files,
    uint64_t configured_memory_limit_bytes,
    bool configured_memory_limit_defined,
    std::string initial_password)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      archive_(archive),
      hooks_(hooks),
      archive_path_(std::move(archive_path)),
      configured_memory_limit_bytes_(configured_memory_limit_bytes),
      configured_memory_limit_defined_(configured_memory_limit_defined &&
                                       configured_memory_limit_bytes != 0),
      total_files_(total_files),
      password_(std::move(initial_password)),
      password_defined_(!password_.empty()) {}

void NativeTestExtractCallback::set_hash_bundle(CHashBundle* hash_bundle) {
  std::lock_guard<std::mutex> lock(mutex_);
  hash_bundle_ = hash_bundle;
  hash_current_active_ = false;
  hash_current_is_dir_ = false;
  hash_first_file_set_ = false;
  hash_file_progress_prev_ = 0;
  hash_current_path_.clear();
}

uint64_t NativeTestExtractCallback::completed_files() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_files_;
}

uint64_t NativeTestExtractCallback::error_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_count_;
}

bool NativeTestExtractCallback::totals_known() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return totals_known_;
}

uint64_t NativeTestExtractCallback::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_bytes_;
}

uint64_t NativeTestExtractCallback::completed_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_bytes_;
}

std::string NativeTestExtractCallback::current_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_path_;
}

std::optional<ProgressRatioInfo> NativeTestExtractCallback::ratio_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ratio_input_size_known_ && !ratio_output_size_known_) {
    return std::nullopt;
  }
  ProgressRatioInfo ratio;
  ratio.input_size_known = ratio_input_size_known_;
  ratio.input_size = ratio_input_size_;
  ratio.output_size_known = ratio_output_size_known_;
  ratio.output_size = ratio_output_size_;
  ratio.compressing_mode = false;
  return ratio;
}

bool NativeTestExtractCallback::password_requested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return password_requested_;
}

bool NativeTestExtractCallback::wrong_password() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wrong_password_;
}

std::string NativeTestExtractCallback::diagnostic_message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return diagnostic_message_;
}

STDMETHODIMP NativeTestExtractCallback::QueryInterface(REFIID iid,
                                                       void** out_object) throw() {
  if (out_object == nullptr) {
    return E_INVALIDARG;
  }
  *out_object = nullptr;

  if (iid == IID_IUnknown ||
      iid == IID_IProgress ||
      iid == IID_IArchiveExtractCallback) {
    *out_object = static_cast<IArchiveExtractCallback*>(this);
  } else if (iid == IID_ICryptoGetTextPassword) {
    *out_object = static_cast<ICryptoGetTextPassword*>(this);
  } else if (iid == IID_ICompressProgressInfo) {
    *out_object = static_cast<ICompressProgressInfo*>(this);
  } else if (iid == IID_IArchiveRequestMemoryUseCallback) {
    *out_object = static_cast<IArchiveRequestMemoryUseCallback*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) NativeTestExtractCallback::AddRef() throw() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) NativeTestExtractCallback::Release() throw() {
  const ULONG next = --ref_count_;
  if (next == 0) {
    delete this;
  }
  return next;
}

STDMETHODIMP NativeTestExtractCallback::SetTotal(UInt64 total) throw() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_known_ = true;
    total_bytes_ = total;
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeTestExtractCallback::SetCompleted(
    const UInt64* complete_value) throw() {
  if (complete_value != nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hash_bundle_ == nullptr) {
      completed_bytes_ = *complete_value;
    }
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeTestExtractCallback::SetRatioInfo(const UInt64* in_size,
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

STDMETHODIMP NativeTestExtractCallback::GetStream(
    UInt32 index,
    ISequentialOutStream** out_stream,
    Int32 /* ask_extract_mode */) throw() {
  if (out_stream == nullptr) {
    return E_INVALIDARG;
  }
  *out_stream = nullptr;

  std::string path = archive_get_prop_text(archive_, index, kpidPath);
  if (path.empty()) {
    path = std::to_string(index);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_path_ = path;
    if (hash_bundle_ != nullptr) {
      bool is_dir = false;
      (void)archive_get_prop_bool(archive_, index, kpidIsDir, is_dir);
      hash_current_active_ = true;
      hash_current_is_dir_ = is_dir;
      hash_current_path_ = path;
      hash_file_progress_prev_ = 0;
      hash_bundle_->InitForNewFile();
      if (!is_dir && !hash_first_file_set_) {
        hash_first_file_set_ = true;
        hash_bundle_->FirstFileName = utf8_to_ustring(path);
      }
    }
  }
  emit_progress_snapshot();

  bool should_return_hash_stream = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_return_hash_stream =
        hash_bundle_ != nullptr && hash_current_active_ && !hash_current_is_dir_;
  }
  if (should_return_hash_stream) {
    *out_stream = new NativeHashOutStream(this);
  }
  return check_canceled();
}

STDMETHODIMP NativeTestExtractCallback::PrepareOperation(
    Int32 ask_extract_mode) throw() {
  bool hash_mode = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hash_mode = hash_bundle_ != nullptr;
  }
  if (hash_mode &&
      (ask_extract_mode == NArchive::NExtract::NAskMode::kExtract ||
       ask_extract_mode == NArchive::NExtract::NAskMode::kTest)) {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      path = current_path_;
    }
    if (!path.empty()) {
      emit_log_event(hooks_,
                     OperationStage::kRunning,
                     OutputChannel::kNone,
                     "Hashing " + path);
    }
  } else if (ask_extract_mode == NArchive::NExtract::NAskMode::kTest) {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      path = current_path_;
    }
    if (!path.empty()) {
      emit_log_event(hooks_,
                     OperationStage::kRunning,
                     OutputChannel::kNone,
                     "Testing " + path);
    }
  }
  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeTestExtractCallback::SetOperationResult(Int32 op_res) throw() {
  std::string path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool hash_mode = hash_bundle_ != nullptr;
    const bool hash_active = hash_current_active_;
    const bool hash_is_dir = hash_current_is_dir_;
    if (!hash_mode || (hash_active && !hash_is_dir)) {
      ++completed_files_;
    }
    if (op_res != NArchive::NExtract::NOperationResult::kOK) {
      ++error_count_;
      if (hash_mode && hash_bundle_ != nullptr) {
        ++hash_bundle_->NumErrors;
      }
      if (op_res == NArchive::NExtract::NOperationResult::kWrongPassword) {
        wrong_password_ = true;
      }
    } else if (hash_mode && hash_bundle_ != nullptr && hash_active) {
      hash_bundle_->Final(hash_is_dir, false, utf8_to_ustring(hash_current_path_));
    }
    path = current_path_;
    if (hash_mode) {
      hash_current_active_ = false;
      hash_current_path_.clear();
      hash_current_is_dir_ = false;
      hash_file_progress_prev_ = 0;
    }
  }

  if (op_res != NArchive::NExtract::NOperationResult::kOK) {
    std::string message = test_operation_result_message(op_res);
    if (!path.empty()) {
      message = path + " : " + message;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
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

  emit_progress_snapshot();
  return check_canceled();
}

STDMETHODIMP NativeTestExtractCallback::CryptoGetTextPassword(BSTR* password) throw() {
  if (password == nullptr) {
    return E_INVALIDARG;
  }
  *password = nullptr;

  std::string password_value;
  bool password_defined = false;
  bool wrong_password = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    password_value = password_;
    password_defined = password_defined_;
    wrong_password = wrong_password_;
  }

  if (!password_defined || wrong_password) {
    if (hooks_.ask_password) {
      PasswordPrompt prompt;
      prompt.archive_path = archive_path_;
      prompt.reason_kind = wrong_password
                               ? PasswordPromptReason::kWrongPassword
                               : PasswordPromptReason::kPasswordRequired;
      prompt.reason = wrong_password ? "wrong_password" : "password_required";
      const PasswordReply reply = hooks_.ask_password(prompt);
      if (reply.kind == PasswordReplyKind::kProvide) {
        password_value = reply.password;
        password_defined = true;
        std::lock_guard<std::mutex> lock(mutex_);
        password_ = password_value;
        password_defined_ = true;
        wrong_password_ = false;
      } else {
        std::lock_guard<std::mutex> lock(mutex_);
        password_requested_ = true;
        return E_ABORT;
      }
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      password_requested_ = true;
      wrong_password_ = true;
      return E_ABORT;
    }
  }

  if (password_defined) {
    const UString pass = utf8_to_ustring(password_value);
    const HRESULT pass_res = StringToBstr(pass, password);
    if (pass_res == S_OK && *password != nullptr) {
      return pass_res;
    }
    return pass_res;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  password_requested_ = true;
  return E_ABORT;
}

STDMETHODIMP NativeTestExtractCallback::RequestMemoryUse(UInt32 flags,
                                                         UInt32,
                                                         UInt32,
                                                         const wchar_t* path,
                                                         UInt64 required_size,
                                                         UInt64* allowed_size,
                                                         UInt32* answer_flags) throw() {
  if (allowed_size == nullptr || answer_flags == nullptr) {
    return E_INVALIDARG;
  }

  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
    if (configured_memory_limit_defined_) {
      *allowed_size = configured_memory_limit_bytes_;
    }
  }

  UInt64 current_limit = *allowed_size;
  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
    if (required_size <= current_limit) {
      *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
      return S_OK;
    }
    *answer_flags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
    if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) {
      *answer_flags |= NRequestMemoryAnswerFlags::k_SkipArc;
    }
  }

  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0 &&
      hooks_.ask_memory_limit) {
    MemoryLimitPrompt prompt;
    prompt.required_usage_bytes = required_size;
    prompt.current_limit_bytes = current_limit;
    prompt.current_limit_defined = current_limit != 0;
    prompt.archive_path = archive_path_;
    if (path != nullptr) {
      prompt.file_path = ustring_to_utf8(UString(path));
    }
    prompt.test_mode = true;
    prompt.skip_archive_supported =
        (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) != 0;
    prompt.report_only =
        (flags & NRequestMemoryUseFlags::k_IsReport) != 0;

    const MemoryLimitReply reply = hooks_.ask_memory_limit(prompt);
    switch (reply.action) {
      case MemoryLimitAction::kAllowOnce:
        *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
        return S_OK;
      case MemoryLimitAction::kUpdateLimitAndContinue:
        if (reply.updated_limit_bytes != 0) {
          *allowed_size = reply.updated_limit_bytes;
          if (required_size <= reply.updated_limit_bytes) {
            *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
            return S_OK;
          }
        }
        break;
      case MemoryLimitAction::kSkipOperation:
        if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) {
          *answer_flags = NRequestMemoryAnswerFlags::k_SkipArc |
                          NRequestMemoryAnswerFlags::k_Limit_Exceeded;
          return S_OK;
        }
        *answer_flags = NRequestMemoryAnswerFlags::k_Stop;
        return E_ABORT;
      case MemoryLimitAction::kCancelOperation:
        *answer_flags = NRequestMemoryAnswerFlags::k_Stop;
        return E_ABORT;
    }
  }

  if ((flags & NRequestMemoryUseFlags::k_NoErrorMessage) == 0) {
    const uint64_t required_mb = required_size >> 20;
    const uint64_t allowed_mb = current_limit >> 20;
    std::string message = "Memory usage limit exceeded";
    message += " (required " + std::to_string(required_mb) + " MB, allowed " +
               std::to_string(allowed_mb) + " MB)";
    if (!archive_path_.empty()) {
      message += " for " + archive_path_;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!diagnostic_message_.empty()) {
        diagnostic_message_ += '\n';
      }
      diagnostic_message_ += message;
      ++error_count_;
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kStdErr,
                   message);
    emit_progress_snapshot();
  }

  return S_OK;
}

NativeTestExtractCallback::ProgressSnapshot
NativeTestExtractCallback::snapshot_progress() const {
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

void NativeTestExtractCallback::emit_progress_snapshot() const {
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

HRESULT NativeTestExtractCallback::check_canceled() const {
  return should_abort() ? E_ABORT : S_OK;
}

HRESULT NativeTestExtractCallback::write_hash_data(const void* data,
                                                   UInt32 size,
                                                   UInt32* processed_size) {
  if (processed_size != nullptr) {
    *processed_size = 0;
  }
  if (data == nullptr && size != 0) {
    return E_INVALIDARG;
  }

  bool should_emit = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hash_bundle_ == nullptr || !hash_current_active_ || hash_current_is_dir_) {
      return E_FAIL;
    }
    hash_bundle_->Update(data, size);
    completed_bytes_ += size;
    if (hash_bundle_->CurSize - hash_file_progress_prev_ >= kHashProgressStepBytes) {
      hash_file_progress_prev_ = hash_bundle_->CurSize;
      should_emit = true;
    }
  }
  if (processed_size != nullptr) {
    *processed_size = size;
  }
  if (should_emit) {
    emit_progress_snapshot();
  }
  return check_canceled();
}

}  // namespace z7::app
