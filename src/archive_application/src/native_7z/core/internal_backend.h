// src/archive_application/src/native_7z/core/internal_backend.h
// Role: NativeArchiveBackend class and remaining operation declarations.

#pragma once

#include "core/internal_results.h"

namespace z7::app {

struct TestArchiveItemStats {
  uint64_t num_dirs = 0;
  uint64_t num_files = 0;
  uint64_t total_unpacked_size = 0;
};

TestArchiveItemStats collect_test_archive_item_stats(IInArchive* archive,
                                                     UInt32 num_items);
void accumulate_test_item_stats(IInArchive* archive,
                                UInt32 index,
                                TestArchiveItemStats& stats);

struct ExtractArchiveItemStats {
  uint64_t num_dirs = 0;
  uint64_t num_files = 0;
  uint64_t total_unpacked_size = 0;
};

std::string normalize_archive_item_path(const std::string& value);
std::string archive_item_selection_path(IInArchive* archive, UInt32 index);
bool archive_path_matches_selection(
    const std::string& item_path,
    const std::unordered_set<std::string>& selected_entries);
void accumulate_extract_item_stats(IInArchive* archive,
                                   UInt32 index,
                                   ExtractArchiveItemStats& stats);
fs::path make_unique_destination_path(const fs::path& original_path,
                                      std::error_code& ec);

std::string update_wide_name_to_utf8(const wchar_t* name);
std::string update_error_message_to_utf8(const CUpdateErrorInfo& error_info);

int list_archive_entries_via_original_api(const std::string& archive_path,
                                          const std::string& directory,
                                          const std::string& archive_type_hint,
                                          bool recursive_dirs,
                                          bool include_detailed_props,
                                          const ArchiveBackendHooks& hooks,
                                          std::atomic<bool>* cancel_requested,
                                          std::function<bool()> wait_while_paused,
                                          CCodecs* preloaded_codecs,
                                          std::vector<ArchiveListEntry>& out_entries,
                                          size_t batch_size = 0,
                                          const std::function<bool(std::vector<ArchiveListEntry>&&)>& batch_callback = {});
// Listing variant that reuses an already-opened CArc (from session registry),
// skipping the open/codecs-load pipeline.
int list_archive_entries_from_arc(const CArc* arc,
                                  const std::string& directory,
                                  bool recursive_dirs,
                                  bool include_detailed_props,
                                  std::atomic<bool>* cancel_requested,
                                  std::vector<ArchiveListEntry>& out_entries,
                                  size_t batch_size = 0,
                                  const std::function<bool(std::vector<ArchiveListEntry>&&)>& batch_callback = {});
int collect_archive_properties_via_original_api(
    const ArchivePropertiesRequest& request,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    CCodecs* preloaded_codecs,
    std::vector<ArchivePropertyLine>& out_lines);
int collect_archive_properties_from_open_state(
    const ArchivePropertiesRequest& request,
    const CArc& arc,
    CCodecs& codecs,
    const CArchiveLink& archive_link,
    std::atomic<bool>* cancel_requested,
    std::vector<ArchivePropertyLine>& out_lines);

struct HashInputEntry {
  fs::path absolute_path;
  std::string relative_path;
  bool is_dir = false;
  uint64_t file_size = 0;
};

std::string path_leaf_name(const fs::path& path);
void collect_hash_entries_for_path(const fs::path& selected_path,
                                   const std::string& display_name,
                                   bool recursive_dirs,
                                   std::vector<HashInputEntry>& entries,
                                   uint64_t& total_files,
                                   uint64_t& total_bytes);
HashSummary make_hash_summary(const CHashBundle& bundle);

class ScopedAtomicBoolReset {
 public:
  explicit ScopedAtomicBoolReset(std::atomic<bool>& value);
  ~ScopedAtomicBoolReset();

 private:
  std::atomic<bool>& value_;
};

class ScopedConditionNotify {
 public:
  explicit ScopedConditionNotify(std::condition_variable& cv);
  ~ScopedConditionNotify();

 private:
  std::condition_variable& cv_;
};

void emit_hash_progress(const ArchiveBackendHooks& hooks,
                        const std::string& line,
                        bool totals_known,
                        uint64_t total_bytes,
                        uint64_t completed_bytes,
                        uint64_t total_files,
                        uint64_t completed_files,
                        uint64_t error_count,
                        const std::string& current_path);

class NativeArchiveBackend final : public INativeArchiveBackend {
 public:
  const char* backend_name() const override;
  BackendCapabilities capabilities() const override;
  NativeInvokeResult invoke(const ArchiveRequest& request,
                            const ArchiveBackendHooks& callbacks) override;

  AddResult add(const AddRequest& request,
                const ArchiveBackendHooks& hooks = {});
  ExtractResult extract(const ExtractRequest& request,
                        const ArchiveBackendHooks& hooks = {});
  TestResult test(const TestRequest& request,
                  const ArchiveBackendHooks& hooks = {});

  HashResult hash(const HashRequest& request,
                  const ArchiveBackendHooks& hooks = {});
  DeleteResult remove(const DeleteRequest& request,
                      const ArchiveBackendHooks& hooks = {});

  BenchmarkResult benchmark(const BenchmarkRequest& request,
                            const ArchiveBackendHooks& hooks = {});
  SplitResult split(const SplitRequest& request,
                    const ArchiveBackendHooks& hooks = {});
  CombineResult combine(const CombineRequest& request,
                        const ArchiveBackendHooks& hooks = {});

  OpenArchiveResult open_archive(const OpenArchiveRequest& request,
                                 const ArchiveBackendHooks& hooks = {});
  OpenArchiveSessionResult open_archive_from_path(
      const OpenArchiveFromPathRequest& request,
      const ArchiveBackendHooks& hooks = {});
  OpenArchiveSessionResult open_archive_from_parent(
      const OpenArchiveFromParentRequest& request,
      const ArchiveBackendHooks& hooks = {});
  OperationResult close_archive_session(const CloseArchiveSessionRequest& request,
                                        const ArchiveBackendHooks& hooks = {});
  ListResult list(const ListRequest& request,
                  const ArchiveBackendHooks& hooks = {});
  ArchivePropertiesResult properties(const ArchivePropertiesRequest& request,
                                     const ArchiveBackendHooks& hooks = {});
  NavigateResult navigate(const NavigateRequest& request,
                          const ArchiveBackendHooks& hooks = {});
  CopyResult copy(const CopyRequest& request,
                  const ArchiveBackendHooks& hooks = {});
  MoveResult move(const MoveRequest& request,
                  const ArchiveBackendHooks& hooks = {});
  RenameResult rename(const RenameRequest& request,
                      const ArchiveBackendHooks& hooks = {});
  CreateResult create(const CreateRequest& request,
                      const ArchiveBackendHooks& hooks = {});
  ArchiveCommentResult comment_archive(const ArchiveCommentRequest& request,
                                       const ArchiveBackendHooks& hooks = {});
  FilesystemCommentResult comment_filesystem(
      const FilesystemCommentRequest& request,
      const ArchiveBackendHooks& hooks = {});
  GetEntryInfoResult get_entry_info(const GetEntryInfoRequest& request,
                                    const ArchiveBackendHooks& hooks = {});

  void cancel() override;
  bool supports_pause() const override;
  void pause() override;
  void resume() override;

 private:
  template <typename TRequest, typename TResult>
  friend class OperationRunner;

  template <typename Handler>
  auto run_with_pauseable_operation(std::atomic<bool>& active_flag, Handler&& handler)
      -> decltype(handler()) {
    cancel_requested_.store(false);
    pause_requested_.store(false);
    active_flag.store(true);
    ScopedAtomicBoolReset reset_cancel(cancel_requested_);
    ScopedAtomicBoolReset reset_pause(pause_requested_);
    ScopedAtomicBoolReset reset_active(active_flag);
    ScopedConditionNotify pause_notify(pause_cv_);
    return handler();
  }

  template <typename Handler>
  auto run_with_cancelable_operation(Handler&& handler) -> decltype(handler()) {
    cancel_requested_.store(false);
    ScopedAtomicBoolReset reset_cancel(cancel_requested_);
    return handler();
  }

  template <typename TResult, typename Handler>
  TResult run_with_operation_codecs(Handler&& handler) {
    if (bound_codecs_ != nullptr) {
      return handler(*bound_codecs_);
    }
    return run_with_loaded_codecs<TResult>(std::forward<Handler>(handler));
  }

  template <typename TResult, typename Invoke>
  TResult run_with_operation_codecs_hresult(Invoke&& invoke,
                                            std::string success_summary = "Success") {
    return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
      TResult out;
      const HRESULT hr = invoke(codecs, out);
      if (hr == S_OK) {
        OperationResult base = static_cast<OperationResult>(
            make_operation_success<TResult>(std::move(success_summary)));
        static_cast<OperationResult&>(out) = std::move(base);
      } else {
        OperationResult base = static_cast<OperationResult>(
            make_operation_failure_from_hresult<TResult>(hr));
        static_cast<OperationResult&>(out) = std::move(base);
      }
      return out;
    });
  }

  template <typename TResult, typename Configure>
  TResult run_update_operation_pipeline(const std::string& archive_path,
                                        const ArchiveBackendHooks& hooks,
                                        NativeUpdateOperationCallback& callback,
                                        Configure&& configure) {
    return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
      CObjectVector<COpenType> types;
      NWildcard::CCensor censor;
      CUpdateOptions options;
      if (std::optional<OperationResult> invalid =
              configure(codecs, types, censor, options);
          invalid.has_value()) {
        return from_base_result<TResult>(std::move(*invalid));
      }

      CUpdateErrorInfo error_info;
      const UpdateOperationStatus status = run_update_archive_shared(&codecs,
                                                                     types,
                                                                     archive_path,
                                                                     censor,
                                                                     options,
                                                                     error_info,
                                                                     callback);
      return finalize_update_operation_result<TResult>(hooks, cancel_requested_, status);
    });
  }

  template <typename TResult, typename Configure, typename CallbackFactory>
  TResult run_update_operation_with_mode(const std::string& archive_path,
                                         const ArchiveBackendHooks& hooks,
                                         uint64_t total_files_hint,
                                         CallbackFactory&& callback_factory,
                                         Configure&& configure) {
    auto callback = callback_factory();
    if (total_files_hint != 0) {
      callback.set_total_files_hint(total_files_hint);
    }
    return run_update_operation_pipeline<TResult>(
        archive_path, hooks, callback, std::forward<Configure>(configure));
  }

  template <typename TResult, typename Handler>
  TResult run_open_archive_read_pipeline(const std::string& archive_path,
                                         const std::string& archive_type_hint,
                                         const ArchiveBackendHooks& hooks,
                                         bool enable_open_callback,
                                         Handler&& handler) {
    return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
      return run_with_open_archive_read<TResult>(archive_path,
                                                 archive_type_hint,
                                                 hooks,
                                                 &cancel_requested_,
                                                 [this]() { return this->wait_while_paused(); },
                                                 enable_open_callback,
                                                 &codecs,
                                                 std::forward<Handler>(handler));
    });
  }

  bool wait_while_paused();
  HashResult run_hash_internal(const HashRequest& request,
                               const ArchiveBackendHooks& hooks);
  HashResult run_hash_entries(const HashRequest& request,
                              const ArchiveBackendHooks& hooks,
                              const std::vector<HashInputEntry>& entries,
                              const std::string& main_name = {});

  std::atomic<bool> cancel_requested_{false};
  std::atomic<bool> hashing_active_{false};
  std::atomic<bool> testing_active_{false};
  std::atomic<bool> extracting_active_{false};
  std::atomic<bool> updating_active_{false};
  std::atomic<bool> benchmarking_active_{false};
  std::atomic<bool> pause_requested_{false};
  mutable std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
  CCodecs* bound_codecs_ = nullptr;
};

}  // namespace z7::app
