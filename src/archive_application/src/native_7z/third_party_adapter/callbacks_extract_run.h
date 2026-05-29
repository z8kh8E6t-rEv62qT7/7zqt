// src/archive_application/src/native_7z/third_party_adapter/callbacks_extract_run.h
// Role: Extract-mode callback declarations.

#pragma once

#include "callback_base.h"
#include "core/internal.h"

namespace z7::app {

class NativeFileOutStream;

class NativeExtractCallback final : public IArchiveExtractCallback,
                                    public ICryptoGetTextPassword,
                                    public ICompressProgressInfo,
                                    public IArchiveRequestMemoryUseCallback,
                                    protected CallbackBase {
 public:
  NativeExtractCallback(IInArchive* archive,
                        fs::path output_dir,
                        const ArchiveBackendHooks& hooks,
                        std::atomic<bool>* cancel_requested,
                        std::function<bool()> wait_while_paused,
                        std::string archive_path,
                        std::vector<std::string> selected_entries,
                        OverwriteMode overwrite_mode,
                        ExtractPathMode path_mode,
                        std::string eliminate_prefix,
                        std::vector<ExtractPathRemap> path_remaps,
                        std::string password,
                        ExtractZoneIdMode zone_id_mode,
                        bool restore_file_security,
                        uint64_t total_files,
                        std::optional<ExtractBudget> budget = std::nullopt,
                        uint64_t configured_memory_limit_bytes = 0,
                        bool configured_memory_limit_defined = false);

  // When configured, extracted bytes are written into the caller-owned buffer
  // instead of a filesystem path. Used by the in-memory nested-archive
  // strategy. The buffer must outlive the extract call.
  void set_buffer_sink(std::vector<uint8_t>* buffer_sink, size_t max_size);

  uint64_t completed_files() const;
  uint64_t error_count() const;
  bool totals_known() const;
  uint64_t total_bytes() const;
  uint64_t completed_bytes() const;
  std::string current_path() const;
  std::optional<ProgressRatioInfo> ratio_info() const;
  bool password_requested() const;
  bool wrong_password() const;
  bool has_io_error() const;
  std::string io_error_message() const;
  std::string diagnostic_message() const;

  // Move out the list of materialized entries; safe to call once after the
  // 7z Extract() invocation completes (and before Release()).
  std::vector<ExtractMaterializedEntry> take_materialized_entries();
  std::vector<ExtractRollbackEntry> take_rollback_entries();

  // Budget state accessors (called after Extract() returns, before Release()).
  bool budget_triggered() const;
  std::string budget_trigger_reason() const;
  BudgetExceededAction budget_policy() const;

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
  struct ResolvedPath {
    fs::path destination_path;
    std::string absolute_output_path;
    bool remapped = false;
  };
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
  struct PendingEntry;

  ProgressSnapshot snapshot_progress() const;
  void emit_progress_snapshot() const;
  void record_io_error(const std::string& message);
  bool close_pending_entry_stream_locked(PendingEntry& pending_entry,
                                         std::string* close_error_message);
  bool commit_pending_entry_locked(PendingEntry& pending_entry,
                                   std::string* commit_error_message);
  bool restore_pending_entry_original_locked(
      PendingEntry& pending_entry,
      std::string* restore_error_message);
  void apply_zone_identifier_to_file(const fs::path& output_path) const;
  HRESULT check_canceled() const;
  OverwriteDecision ask_overwrite_decision(const fs::path& destination_path,
                                           UInt32 index,
                                           const std::string& item_path);
  ResolvedPath resolve_destination_path(const std::string& item_path) const;
  std::string normalize_path_for_output(std::string item_path) const;
  static bool is_absolute_item_path(const std::string& path);
  static std::string base_name_for_no_paths(const std::string& path);
  bool request_selects_single_logical_root() const;

  std::atomic<ULONG> ref_count_{1};
  IInArchive* archive_ = nullptr;
  fs::path output_dir_;
  const ArchiveBackendHooks& hooks_;
  std::string archive_path_;
  std::vector<std::string> selected_entries_;
  OverwriteMode overwrite_mode_ = OverwriteMode::kAsk;
  ExtractPathMode path_mode_ = ExtractPathMode::kFullPaths;
  std::string eliminate_prefix_;
  std::vector<ExtractPathRemap> path_remaps_;
  std::string password_;
  ExtractZoneIdMode zone_id_mode_ = ExtractZoneIdMode::kNone;
  bool restore_file_security_ = false;
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
  bool password_requested_ = false;
  bool wrong_password_ = false;
  bool io_error_ = false;
  bool ask_mode_notice_emitted_ = false;
  bool ask_yes_to_all_ = false;
  bool ask_no_to_all_ = false;
  bool security_notice_emitted_ = false;
  bool current_item_encrypted_ = false;
  std::string io_error_message_;
  std::string diagnostic_message_;

  struct PendingEntry {
    std::string archive_entry_path;
    std::string absolute_output_path;
    fs::path output_path;
    fs::path staged_output_path;
    fs::path destination_path;
    fs::path backup_path;
    bool had_original = false;
    bool overwrote_existing = false;
    bool renamed_from_collision = false;
    bool restore_backup_on_failure = false;
    bool preserve_backup_on_commit = false;
    bool preserve_committed_backup_for_rollback = false;
    uint64_t declared_size = 0;  // kpidSize from archive header
    NativeFileOutStream* owned_stream = nullptr;
  };
  std::vector<ExtractMaterializedEntry> materialized_entries_;
  std::vector<ExtractRollbackEntry> rollback_entries_;
  std::optional<PendingEntry> pending_entry_;

  // Budget enforcement (optional). Set by constructor when request.budget is present.
  std::optional<ExtractBudget> budget_;
  std::atomic<uint64_t> budget_files_seen_{0};
  std::atomic<uint64_t> budget_bytes_seen_{0};
  std::atomic<bool> budget_triggered_{false};
  std::string budget_trigger_reason_;  // protected by mutex_

  // In-memory sink (optional). When non-null, GetStream skips all filesystem
  // bookkeeping and writes into this buffer up to buffer_sink_max_size_.
  std::vector<uint8_t>* buffer_sink_ = nullptr;
  size_t buffer_sink_max_size_ = 0;
};

}  // namespace z7::app
