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
                              ArchiveBackendHooks const& hooks,
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
        void finish_deferred_links();

        // Budget state accessors (called after Extract() returns, before Release()).
        bool budget_triggered() const;
        std::string budget_trigger_reason() const;
        BudgetExceededAction budget_policy() const;

        STDMETHOD(QueryInterface)(REFIID iid, void** out_object) throw() override;
        STDMETHOD_(ULONG, AddRef)() throw() override;
        STDMETHOD_(ULONG, Release)() throw() override;

        STDMETHOD(SetTotal)(UInt64 total) throw() override;
        STDMETHOD(SetCompleted)(UInt64 const* complete_value) throw() override;
        STDMETHOD(SetRatioInfo)(UInt64 const* in_size, UInt64 const* out_size) throw() override;
        STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream** out_stream, Int32 ask_extract_mode) throw() override;
        STDMETHOD(PrepareOperation)(Int32 ask_extract_mode) throw() override;
        STDMETHOD(SetOperationResult)(Int32 op_res) throw() override;
        STDMETHOD(CryptoGetTextPassword)(BSTR* password) throw() override;
        STDMETHOD(RequestMemoryUse)(UInt32 flags,
                                    UInt32 index_type,
                                    UInt32 index,
                                    wchar_t const* path,
                                    UInt64 required_size,
                                    UInt64* allowed_size,
                                    UInt32* answer_flags) throw() override;

    private:
        struct ResolvedPath {
            fs::path destination_path;
            fs::path authorized_root;
            std::string absolute_output_path;
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

        struct ExtractItemAttributes {
            bool defined = false;
            UInt32 attrib = 0;
        };

        struct ExtractItemLinkInfo {
            enum class Type {
                kNone,
                kSymLink,
                kHardLink
            };
            Type type = Type::kNone;
            std::string target;

            bool is_link() const { return type != Type::kNone; }
        };

        struct OutputTarget {
            std::string archive_entry_path;
            std::string absolute_output_path;
            fs::path output_path;
            fs::path destination_path;
            fs::path authorized_root;
            fs::path backup_path;
            bool had_original = false;
            bool overwrote_existing = false;
            bool renamed_from_collision = false;
            bool preserve_backup_on_commit = false;
        };

        struct LinkCreationPlan {
            ExtractItemLinkInfo::Type type = ExtractItemLinkInfo::Type::kNone;
            std::string symlink_target;
            fs::path hardlink_target_path;
        };

        struct DeferredHardLink {
            OutputTarget output_target;
            fs::path target_path;
        };
        struct PendingEntry;

        ProgressSnapshot snapshot_progress() const;
        void emit_progress_snapshot() const;
        void record_io_error(std::string const& message);
        void record_nonfatal_warning(std::string const& message);
        bool close_pending_entry_stream_locked(PendingEntry& pending_entry, std::string* close_error_message);
        HRESULT read_item_attributes(UInt32 index, ExtractItemAttributes& attributes) const;
        HRESULT read_item_link_info(UInt32 index, ExtractItemLinkInfo& link_info) const;
        std::optional<std::string> apply_item_attributes(fs::path const& output_path,
                                                         ExtractItemAttributes const& attributes) const;
        bool create_output_directories_with_zone_identifier(fs::path const& directory_path, std::error_code& ec) const;
        bool path_is_within_authorized_root(fs::path const& candidate,
                                            fs::path const& authorized_root,
                                            std::error_code& ec) const;
        bool ensure_output_path_is_authorized(fs::path const& path_to_resolve,
                                              fs::path const& authorized_root,
                                              fs::path const& reported_output_path);
        bool try_reserve_declared_budget_bytes(uint64_t declared_size);
        void release_reserved_budget_bytes(uint64_t declared_size);
        HRESULT prepare_output_target(UInt32 index,
                                      std::string const& output_item_path,
                                      ResolvedPath const& resolved_path,
                                      OutputTarget& target,
                                      bool& skipped);
        std::optional<std::string> prepare_link_creation_plan(OutputTarget const& output_target,
                                                              ExtractItemLinkInfo const& link_info,
                                                              LinkCreationPlan& plan) const;
        std::optional<std::string>
        materialize_link(OutputTarget const& output_target, LinkCreationPlan const& plan, bool allow_defer);
        std::optional<std::string> create_symbolic_link(fs::path const& output_path, std::string const& target) const;
        std::optional<std::string> create_hard_link(fs::path const& output_path, fs::path const& target_path) const;
        void record_materialized_output_locked(OutputTarget const& target, uint64_t bytes_written, bool is_directory);
        void apply_zone_identifier_to_output(fs::path const& output_path, bool is_directory) const;
        HRESULT check_canceled() const;
        OverwriteDecision
        ask_overwrite_decision(fs::path const& destination_path, UInt32 index, std::string const& item_path);
        ResolvedPath resolve_destination_path(std::string const& item_path, bool is_directory) const;
        std::string normalize_path_for_output(std::string item_path) const;
        static bool is_absolute_item_path(std::string const& path);
        static std::string base_name_for_no_paths(std::string const& path);
        bool request_selects_single_logical_root() const;

        std::atomic<ULONG> ref_count_{1};
        IInArchive* archive_ = nullptr;
        fs::path output_dir_;
        ArchiveBackendHooks const& hooks_;
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
        bool password_defined_ = false;
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
            fs::path destination_path;
            fs::path backup_path;
            bool had_original = false;
            bool overwrote_existing = false;
            bool renamed_from_collision = false;
            bool preserve_backup_on_commit = false;
            uint64_t declared_size = 0; // kpidSize from archive header
            bool budget_bytes_reserved = false;
            ExtractItemAttributes attributes;
            NativeFileOutStream* owned_stream = nullptr;
        };

        std::vector<ExtractMaterializedEntry> materialized_entries_;
        std::vector<ExtractRollbackEntry> rollback_entries_;
        std::vector<DeferredHardLink> deferred_hard_links_;
        std::optional<PendingEntry> pending_entry_;

        // Budget enforcement (optional). Set by constructor when request.budget is present.
        std::optional<ExtractBudget> budget_;
        std::atomic<uint64_t> budget_files_seen_{0};
        std::atomic<uint64_t> budget_bytes_seen_{0};
        std::atomic<bool> budget_triggered_{false};
        std::string budget_trigger_reason_; // protected by mutex_

        // In-memory sink (optional). When non-null, GetStream skips all filesystem
        // bookkeeping and writes into this buffer up to buffer_sink_max_size_.
        std::vector<uint8_t>* buffer_sink_ = nullptr;
        size_t buffer_sink_max_size_ = 0;
    };

} // namespace z7::app
