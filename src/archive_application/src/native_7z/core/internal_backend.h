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

    TestArchiveItemStats collect_test_archive_item_stats(IInArchive* archive, UInt32 num_items);
    void accumulate_test_item_stats(IInArchive* archive, UInt32 index, TestArchiveItemStats& stats);

    struct ExtractArchiveItemStats {
        uint64_t num_dirs = 0;
        uint64_t num_files = 0;
        uint64_t total_unpacked_size = 0;
    };

    struct ArchiveItemPath {
        std::string resolved;
        std::string normalized;
    };

    std::string normalize_archive_item_path(std::string const& value);
    HRESULT resolve_archive_item_path(CArc const* arc, UInt32 index, ArchiveItemPath& path);
    bool archive_path_matches_selection(std::string const& item_path,
                                        std::unordered_set<std::string> const& selected_entries);
    void accumulate_extract_item_stats(IInArchive* archive, UInt32 index, ExtractArchiveItemStats& stats);
    fs::path make_unique_destination_path(fs::path const& original_path, std::error_code& ec);

    std::string update_wide_name_to_utf8(wchar_t const* name);
    std::string update_error_message_to_utf8(CUpdateErrorInfo const& error_info);

    int list_archive_entries_via_original_api(
        std::string const& archive_path,
        std::string const& directory,
        std::string const& archive_type_hint,
        bool recursive_dirs,
        bool include_detailed_props,
        ArchiveBackendHooks const& hooks,
        std::atomic<bool>* cancel_requested,
        std::function<bool()> wait_while_paused,
        CCodecs* preloaded_codecs,
        std::vector<ArchiveListEntry>& out_entries,
        size_t batch_size = 0,
        std::function<bool(std::vector<ArchiveListEntry>&&)> const& batch_callback = {},
        OpenArchiveDiagnostics* out_diagnostics = nullptr);
    // Listing variant that reuses an already-opened CArc (from session registry),
    // skipping the open/codecs-load pipeline.
    int list_archive_entries_from_arc(CArc const* arc,
                                      std::string const& directory,
                                      bool recursive_dirs,
                                      bool include_detailed_props,
                                      std::atomic<bool>* cancel_requested,
                                      std::vector<ArchiveListEntry>& out_entries,
                                      size_t batch_size = 0,
                                      std::function<bool(std::vector<ArchiveListEntry>&&)> const& batch_callback = {});
    int collect_archive_properties_via_original_api(ArchivePropertiesRequest const& request,
                                                    ArchiveBackendHooks const& hooks,
                                                    std::atomic<bool>* cancel_requested,
                                                    std::function<bool()> wait_while_paused,
                                                    CCodecs* preloaded_codecs,
                                                    std::vector<ArchivePropertyLine>& out_lines,
                                                    OpenArchiveDiagnostics* out_diagnostics = nullptr);
    int collect_archive_properties_from_open_state(ArchivePropertiesRequest const& request,
                                                   CArc const& arc,
                                                   CCodecs& codecs,
                                                   CArchiveLink const& archive_link,
                                                   std::atomic<bool>* cancel_requested,
                                                   std::vector<ArchivePropertyLine>& out_lines);

    struct HashInputEntry {
        fs::path absolute_path;
        std::string relative_path;
        bool is_dir = false;
        uint64_t file_size = 0;
        UInt32 archive_index = static_cast<UInt32>(-1);
    };

    struct HashScanError {
        fs::path path;
        std::error_code error;
    };

    std::string path_leaf_name(fs::path const& path);
    void collect_hash_entries_for_path(fs::path const& selected_path,
                                       std::string const& display_name,
                                       bool recursive_dirs,
                                       std::vector<HashInputEntry>& entries,
                                       std::vector<HashScanError>& scan_errors,
                                       uint64_t& total_files,
                                       uint64_t& total_bytes);
    HashSummary make_hash_summary(CHashBundle const& bundle);

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

    void emit_hash_progress(ArchiveBackendHooks const& hooks,
                            std::string const& line,
                            bool totals_known,
                            uint64_t total_bytes,
                            uint64_t completed_bytes,
                            uint64_t total_files,
                            uint64_t completed_files,
                            uint64_t error_count,
                            std::string const& current_path);

    class NativeArchiveBackend final : public INativeArchiveBackend {
    public:
        char const* backend_name() const override;
        BackendCapabilities capabilities() const override;
        NativeInvokeResult invoke(ArchiveRequest const& request, ArchiveBackendHooks const& callbacks) override;

        AddResult add(AddRequest const& request, ArchiveBackendHooks const& hooks = {});
        ExtractResult extract(ExtractRequest const& request, ArchiveBackendHooks const& hooks = {});
        TestResult test(TestRequest const& request, ArchiveBackendHooks const& hooks = {});

        HashResult hash(HashRequest const& request, ArchiveBackendHooks const& hooks = {});
        DeleteResult remove(DeleteRequest const& request, ArchiveBackendHooks const& hooks = {});

        BenchmarkResult benchmark(BenchmarkRequest const& request, ArchiveBackendHooks const& hooks = {});
        SplitResult split(SplitRequest const& request, ArchiveBackendHooks const& hooks = {});
        CombineResult combine(CombineRequest const& request, ArchiveBackendHooks const& hooks = {});

        OpenArchiveResult open_archive(OpenArchiveRequest const& request, ArchiveBackendHooks const& hooks = {});
        OpenArchiveSessionResult open_archive_from_path(OpenArchiveFromPathRequest const& request,
                                                        ArchiveBackendHooks const& hooks = {});
        OpenArchiveSessionResult open_archive_from_parent(OpenArchiveFromParentRequest const& request,
                                                          ArchiveBackendHooks const& hooks = {});
        OperationResult set_archive_session_filename_code_page(
            SetArchiveSessionFilenameCodePageRequest const& request,
            ArchiveBackendHooks const& hooks = {});
        OperationResult close_archive_session(CloseArchiveSessionRequest const& request,
                                              ArchiveBackendHooks const& hooks = {});
        ListResult list(ListRequest const& request, ArchiveBackendHooks const& hooks = {});
        ArchivePropertiesResult properties(ArchivePropertiesRequest const& request,
                                           ArchiveBackendHooks const& hooks = {});
        NavigateResult navigate(NavigateRequest const& request, ArchiveBackendHooks const& hooks = {});
        CopyResult copy(CopyRequest const& request, ArchiveBackendHooks const& hooks = {});
        MoveResult move(MoveRequest const& request, ArchiveBackendHooks const& hooks = {});
        RenameResult rename(RenameRequest const& request, ArchiveBackendHooks const& hooks = {});
        CreateResult create(CreateRequest const& request, ArchiveBackendHooks const& hooks = {});
        ArchiveCommentResult comment_archive(ArchiveCommentRequest const& request,
                                             ArchiveBackendHooks const& hooks = {});
        FilesystemCommentResult comment_filesystem(FilesystemCommentRequest const& request,
                                                   ArchiveBackendHooks const& hooks = {});
        GetEntryInfoResult get_entry_info(GetEntryInfoRequest const& request, ArchiveBackendHooks const& hooks = {});

        void cancel() override;
        bool supports_pause() const override;
        void pause() override;
        void resume() override;

    private:
        template <typename TRequest, typename TResult>
        friend class OperationRunner;

        template <typename Handler>
        auto run_with_pauseable_operation(std::atomic<bool>& active_flag, Handler&& handler) -> decltype(handler()) {
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
        TResult run_with_operation_codecs_hresult(Invoke&& invoke, std::string success_summary = "Success") {
            return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
                TResult out;
                const HRESULT hr = invoke(codecs, out);
                if (hr == S_OK) {
                    OperationResult base =
                        static_cast<OperationResult>(make_operation_success<TResult>(std::move(success_summary)));
                    static_cast<OperationResult&>(out) = std::move(base);
                } else {
                    OperationResult base =
                        static_cast<OperationResult>(make_operation_failure_from_hresult<TResult>(hr));
                    static_cast<OperationResult&>(out) = std::move(base);
                }
                return out;
            });
        }

        template <typename TResult, typename Configure>
        TResult run_update_operation_pipeline(std::string const& archive_path,
                                              ArchiveBackendHooks const& hooks,
                                              NativeUpdateOperationCallback& callback,
                                              Configure&& configure) {
            return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
                CObjectVector<COpenType> types;
                NWildcard::CCensor censor;
                CUpdateOptions options;
                if (std::optional<OperationResult> invalid = configure(codecs, types, censor, options);
                    invalid.has_value()) {
                    return from_base_result<TResult>(std::move(*invalid));
                }

                CUpdateErrorInfo error_info;
                UpdateOperationStatus const status =
                    run_update_archive_shared(&codecs, types, archive_path, censor, options, error_info, callback);
                return finalize_update_operation_result<TResult>(hooks, cancel_requested_, status);
            });
        }

        template <typename TResult, typename Configure, typename CallbackFactory>
        TResult run_update_operation_with_mode(std::string const& archive_path,
                                               ArchiveBackendHooks const& hooks,
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
        TResult run_open_archive_read_pipeline(std::string const& archive_path,
                                               std::string const& archive_type_hint,
                                               ArchiveBackendHooks const& hooks,
                                               OpenResultMessagePolicy message_policy,
                                               bool allow_password_prompt,
                                               std::string const& initial_password,
                                               Handler&& handler) {
            return run_with_operation_codecs<TResult>([&](CCodecs& codecs) -> TResult {
                return run_with_open_archive_read<TResult>(
                    archive_path,
                    archive_type_hint,
                    hooks,
                    &cancel_requested_,
                    [this]() { return this->wait_while_paused(); },
                    message_policy,
                    allow_password_prompt,
                    initial_password,
                    &codecs,
                    std::forward<Handler>(handler));
            });
        }

        bool wait_while_paused();
        HashResult run_hash_internal(HashRequest const& request, ArchiveBackendHooks const& hooks);
        HashResult run_hash_entries(HashRequest const& request,
                                    ArchiveBackendHooks const& hooks,
                                    std::vector<HashInputEntry> const& entries,
                                    std::string const& main_name = {},
                                    uint64_t initial_error_count = 0);
        HashResult run_hash_archive_entries(HashRequest const& request,
                                            ArchiveBackendHooks const& hooks,
                                            CArc const* arc,
                                             std::vector<HashInputEntry> const& entries,
                                             std::string const& main_name,
                                             std::string const& archive_display_path,
                                             std::string const& password,
                                             OpenArchiveDiagnostics const* open_diagnostics = nullptr);

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

} // namespace z7::app
