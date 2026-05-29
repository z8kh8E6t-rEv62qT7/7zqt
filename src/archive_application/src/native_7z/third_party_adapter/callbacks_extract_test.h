// src/archive_application/src/native_7z/third_party_adapter/callbacks_extract_test.h
// Role: Test-mode extraction callback declarations.

#pragma once

#include <optional>

#include "archive_types.h"
#include "callback_base.h"

namespace z7::app {

class NativeTestExtractCallback final : public IArchiveExtractCallback,
                                        public ICryptoGetTextPassword,
                                        public ICompressProgressInfo,
                                        public IArchiveRequestMemoryUseCallback,
                                        protected CallbackBase {
 public:
  NativeTestExtractCallback(IInArchive* archive,
                            const ArchiveBackendHooks& hooks,
                            std::atomic<bool>* cancel_requested,
                            std::function<bool()> wait_while_paused,
                            std::string archive_path,
                            uint64_t total_files,
                            uint64_t configured_memory_limit_bytes = 0,
                            bool configured_memory_limit_defined = false);

  uint64_t completed_files() const;
  uint64_t error_count() const;
  bool totals_known() const;
  uint64_t total_bytes() const;
  uint64_t completed_bytes() const;
  std::string current_path() const;
  std::optional<ProgressRatioInfo> ratio_info() const;
  bool password_requested() const;
  bool wrong_password() const;
  std::string diagnostic_message() const;

  STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
  STDMETHOD_(ULONG, AddRef)() throw() override;
  STDMETHOD_(ULONG, Release)() throw() override;

  STDMETHOD(SetTotal)(UInt64 total) throw() override;
  STDMETHOD(SetCompleted)(const UInt64* complete_value) throw() override;
  STDMETHOD(SetRatioInfo)(const UInt64* in_size,
                          const UInt64* out_size) throw() override;
  STDMETHOD(GetStream)(UInt32 index,
                       ISequentialOutStream** out_stream,
                       Int32 ask_extract_mode) throw() override;
  STDMETHOD(PrepareOperation)(Int32 ask_extract_mode) throw() override;
  STDMETHOD(SetOperationResult)(Int32 op_res) throw() override;
  STDMETHOD(CryptoGetTextPassword)(BSTR* password) throw() override;
  STDMETHOD(RequestMemoryUse)(UInt32 flags,
                              UInt32 index_type,
                              UInt32 index,
                              const wchar_t* path,
                              UInt64 required_size,
                              UInt64* allowed_size,
                              UInt32* answer_flags) throw() override;

 private:
  struct ProgressSnapshot {
    bool totals_known = false;
    uint64_t total_bytes = 0;
    uint64_t completed_bytes = 0;
    uint64_t total_files = 0;
    uint64_t completed_files = 0;
    uint64_t error_count = 0;
    std::string current_path;
    std::optional<ProgressRatioInfo> ratio_info;
  };

  ProgressSnapshot snapshot_progress() const;
  void emit_progress_snapshot() const;
  HRESULT check_canceled() const;

  std::atomic<ULONG> ref_count_{1};
  IInArchive* archive_ = nullptr;
  const ArchiveBackendHooks& hooks_;
  std::string archive_path_;
  uint64_t configured_memory_limit_bytes_ = 0;
  bool configured_memory_limit_defined_ = false;

  mutable std::mutex mutex_;
  bool totals_known_ = false;
  uint64_t total_bytes_ = 0;
  uint64_t completed_bytes_ = 0;
  uint64_t total_files_ = 0;
  uint64_t completed_files_ = 0;
  uint64_t error_count_ = 0;
  std::string current_path_;
  bool ratio_input_size_known_ = false;
  uint64_t ratio_input_size_ = 0;
  bool ratio_output_size_known_ = false;
  uint64_t ratio_output_size_ = 0;
  std::string password_;
  bool password_requested_ = false;
  bool wrong_password_ = false;
  std::string diagnostic_message_;
};

}  // namespace z7::app
