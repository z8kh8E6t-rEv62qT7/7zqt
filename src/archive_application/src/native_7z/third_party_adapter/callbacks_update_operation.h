// src/archive_application/src/native_7z/third_party_adapter/callbacks_update_operation.h
// Role: Update/delete operation callback declarations.

#pragma once

#include <optional>

#include "archive_types.h"
#include "callback_base.h"

namespace z7::app {

class NativeUpdateOperationCallback final : public IUpdateCallbackUI2,
                                            public IOpenCallbackUI,
                                            protected CallbackBase {
 public:
  enum class Mode {
    kAdd,
    kDelete
  };

  NativeUpdateOperationCallback(const ArchiveBackendHooks& hooks,
                                std::atomic<bool>* cancel_requested,
                                std::function<bool()> wait_while_paused,
                                std::string archive_path,
                                Mode mode,
                                std::string initial_password = {});

  bool totals_known() const;
  uint64_t total_bytes() const;
  uint64_t completed_bytes() const;
  uint64_t total_files() const;
  uint64_t completed_files() const;
  uint64_t error_count() const;
  std::string current_path() const;
  std::optional<ProgressRatioInfo> ratio_info() const;
  std::string password() const;
  bool password_requested() const;
  bool wrong_password() const;

  void set_total_files_hint(uint64_t total_files);

  HRESULT OpenResult(const CCodecs*,
                     const CArchiveLink&,
                     const wchar_t* name,
                     HRESULT result) override;
  HRESULT StartScanning() override;
  HRESULT FinishScanning(const CDirItemsStat& st) override;
  HRESULT StartOpenArchive(const wchar_t* name) override;
  HRESULT StartArchive(const wchar_t* name, bool) override;
  HRESULT FinishArchive(const CFinishArchiveStat&) override;
  HRESULT DeletingAfterArchiving(const FString& path, bool) override;
  HRESULT FinishDeletingAfterArchiving() override;
  HRESULT MoveArc_Start(const wchar_t*,
                        const wchar_t*,
                        UInt64 size,
                        Int32) override;
  HRESULT MoveArc_Progress(UInt64 total, UInt64 current) override;
  HRESULT MoveArc_Finish() override;
  HRESULT WriteSfx(const wchar_t*, UInt64) override;
  HRESULT SetTotal(UInt64 size) override;
  HRESULT SetCompleted(const UInt64* complete_value) override;
  HRESULT SetRatioInfo(const UInt64*, const UInt64*) override;
  HRESULT CheckBreak() override;
  HRESULT SetNumItems(const CArcToDoStat& stat) override;
  HRESULT GetStream(const wchar_t* name, bool, bool is_anti, UInt32) override;
  HRESULT OpenFileError(const FString& path, DWORD) override;
  HRESULT ReadingFileError(const FString& path, DWORD) override;
  HRESULT SetOperationResult(Int32 op_res) override;
  HRESULT ReportExtractResult(Int32 op_res,
                              Int32,
                              const wchar_t* name) override;
  HRESULT ReportUpdateOperation(UInt32,
                                const wchar_t* name,
                                bool) override;

#ifndef Z7_NO_CRYPTO
  HRESULT CryptoGetTextPassword2(Int32* password_is_defined,
                                 BSTR* password) override;
  HRESULT CryptoGetTextPassword(BSTR* password) override;
#endif

  HRESULT ShowDeleteFile(const wchar_t* name, bool) override;
  HRESULT ScanError(const FString& path, DWORD) override;
  HRESULT ScanProgress(const CDirItemsStat& st,
                       const FString& path,
                       bool) override;
  HRESULT Open_CheckBreak() override;
  HRESULT Open_SetTotal(const UInt64* files, const UInt64* bytes) override;
  HRESULT Open_SetCompleted(const UInt64* files,
                            const UInt64* bytes) override;
  HRESULT Open_Finished() override;

#ifndef Z7_NO_CRYPTO
  HRESULT Open_CryptoGetTextPassword(BSTR* password) override;
#endif

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
  void note_error(const std::string& message);
  HRESULT check_break() const;
  HRESULT provide_password(BSTR* password, bool force_prompt);

  ArchiveBackendHooks hooks_;
  std::string archive_path_;
  Mode mode_ = Mode::kAdd;

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
};

}  // namespace z7::app
