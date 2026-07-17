// src/archive_application/src/native_7z/callbacks/callbacks_extract_run.cpp
// Role: Extract callback construction and externally queried state.

#include "third_party_adapter/callbacks_extract_run.h"

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_stream.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    NativeExtractCallback::NativeExtractCallback(CArc const* arc,
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
                                                 std::optional<ExtractBudget> budget,
                                                 std::shared_ptr<ExtractBudgetTracker> budget_tracker,
                                                 uint64_t configured_memory_limit_bytes,
                                                 bool configured_memory_limit_defined,
                                                 std::string archive_metadata_source_path,
                                                 uint64_t initial_progress_error_count,
                                                 bool archive_context_already_reported) :
        CallbackBase(cancel_requested, std::move(wait_while_paused)),
        arc_(arc),
        archive_(arc != nullptr ? arc->Archive : nullptr),
        output_dir_(std::move(output_dir)),
        hooks_(hooks),
        archive_path_(std::move(archive_path)),
        archive_metadata_source_path_(archive_metadata_source_path.empty() ? archive_path_
                                                                            : std::move(archive_metadata_source_path)),
        selected_entries_(std::move(selected_entries)),
        overwrite_mode_(overwrite_mode),
        path_mode_(path_mode),
        eliminate_prefix_(normalize_archive_item_path(eliminate_prefix)),
        path_remaps_(std::move(path_remaps)),
        password_(std::move(password)),
        zone_id_mode_(zone_id_mode),
        restore_file_security_(restore_file_security),
        configured_memory_limit_bytes_(configured_memory_limit_bytes),
        configured_memory_limit_defined_(configured_memory_limit_defined && configured_memory_limit_bytes != 0),
        total_files_(total_files),
        initial_progress_error_count_(initial_progress_error_count),
        archive_error_path_reported_(archive_context_already_reported),
        budget_tracker_(budget_tracker != nullptr ? std::move(budget_tracker)
                                                  : std::make_shared<ExtractBudgetTracker>(std::move(budget))) {
        password_defined_ = !password_.empty();
        for (std::string& entry : selected_entries_) {
            entry = normalize_archive_item_path(entry);
        }
        for (ExtractPathRemap& remap : path_remaps_) {
            remap.source_path = normalize_archive_item_path(remap.source_path);
        }
    }

    NativeExtractCallback::~NativeExtractCallback() {
        discard_pending_outputs();
    }

    void NativeExtractCallback::discard_pending_outputs() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto append_cleanup_failure = [this](std::string const& message) {
            io_error_ = true;
            if (io_error_message_.empty()) {
                io_error_message_ = message;
            }
            if (!diagnostic_message_.empty()) {
                diagnostic_message_ += '\n';
            }
            diagnostic_message_ += message;
            ++error_count_;
        };
        if (pending_entry_.has_value()) {
            discard_pending_entry_locked(*pending_entry_);
            pending_entry_.reset();
        }
        if (pending_directory_.has_value()) {
            OutputTarget const& target = pending_directory_->output_target;
            if (pending_directory_->created) {
                std::string cleanup_error;
                if (!cleanup_materialized_target_locked(
                        target, pending_directory_->materialized_identity, &cleanup_error)) {
                    append_cleanup_failure(cleanup_error);
                }
            }
            if (pending_directory_->budget_file_reserved) {
                release_budget_file();
            }
            pending_directory_.reset();
        }
        if (pending_link_.has_value()) {
            OutputTarget const target = pending_link_->output_target;
            std::string cleanup_error;
            bool const cleaned =
                cleanup_materialized_target_locked(target, pending_link_->materialized_identity, &cleanup_error);
            if (cleaned) {
                if (!materialized_entries_.empty()
                    && materialized_entries_.back().absolute_output_path == target.absolute_output_path) {
                    materialized_entries_.pop_back();
                }
                if (!rollback_entries_.empty() && rollback_entries_.back().output_path == target.output_path) {
                    rollback_entries_.pop_back();
                }
                materialized_output_targets_.erase(target.archive_index);
            } else {
                append_cleanup_failure(cleanup_error);
            }
            release_budget_file();
            pending_link_.reset();
        }
        if (pending_data_symlink_.has_value()) {
            if (budget_tracker_ != nullptr && pending_data_symlink_->budget_bytes_reserved != 0) {
                budget_tracker_->release_bytes(pending_data_symlink_->budget_bytes_reserved);
            }
            release_budget_file();
            if (pending_data_symlink_->output_target.transaction != nullptr) {
                std::string finish_diagnostic;
                if (!pending_data_symlink_->output_target.transaction->finish(&finish_diagnostic)) {
                    append_cleanup_failure(finish_diagnostic);
                }
            }
            pending_data_symlink_.reset();
        }
        if (pending_alternate_stream_.has_value()) {
            if (std::optional<std::string> const cleanup =
                    discard_pending_alternate_stream(*pending_alternate_stream_, true);
                cleanup.has_value()) {
                append_cleanup_failure(*cleanup);
            }
            pending_alternate_stream_.reset();
        }
        for (size_t i = 0; i < deferred_hard_links_.size(); ++i) {
            release_budget_file();
        }
        deferred_hard_links_.clear();
        pending_deferred_hard_link_output_.reset();
    }

    void NativeExtractCallback::set_single_item_output_stream(ISequentialOutStream* output_stream) {
        single_item_output_stream_ = output_stream;
    }

    uint64_t NativeExtractCallback::completed_files() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_files_;
    }

    uint64_t NativeExtractCallback::error_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_count_;
    }

    bool NativeExtractCallback::totals_known() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return totals_known_;
    }

    uint64_t NativeExtractCallback::total_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_bytes_;
    }

    uint64_t NativeExtractCallback::completed_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_bytes_;
    }

    std::string NativeExtractCallback::current_path() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_path_;
    }

    std::optional<ProgressRatioInfo> NativeExtractCallback::ratio_info() const {
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

    bool NativeExtractCallback::password_requested() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return password_requested_;
    }

    bool NativeExtractCallback::wrong_password() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return wrong_password_;
    }

    bool NativeExtractCallback::has_io_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return io_error_
            || (pending_entry_.has_value()
                && pending_entry_->owned_stream != nullptr
                && !pending_entry_->owned_stream->failure_message().empty());
    }

    std::string NativeExtractCallback::io_error_message() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (io_error_message_.empty() && pending_entry_.has_value() && pending_entry_->owned_stream != nullptr) {
            return pending_entry_->owned_stream->failure_message();
        }
        return io_error_message_;
    }

    std::string NativeExtractCallback::diagnostic_message() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return diagnostic_message_;
    }

    bool NativeExtractCallback::memory_skip_handled() const {
        return memory_skip_handled_.load();
    }

    std::vector<ExtractMaterializedEntry> NativeExtractCallback::take_materialized_entries() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (ExtractRollbackEntry const& rollback : rollback_entries_) {
            if (!rollback.preserve_backup_on_commit || rollback.backup_path.empty()) {
                continue;
            }
            std::string const destination_path = report_path_without_following_leaf(rollback.destination_path);
            auto prior = std::find_if(materialized_entries_.begin(),
                                      materialized_entries_.end(),
                                      [&](ExtractMaterializedEntry const& entry) {
                                          return entry.absolute_output_path == destination_path;
                                      });
            if (prior != materialized_entries_.end()) {
                prior->absolute_output_path = report_path_without_following_leaf(rollback.backup_path);
                prior->renamed_from_collision = true;
            }
        }

        std::vector<ExtractMaterializedEntry> compacted;
        compacted.reserve(materialized_entries_.size());
        for (ExtractMaterializedEntry& entry : materialized_entries_) {
            entry.absolute_output_path = report_path_without_following_leaf(entry.absolute_output_path);
            auto equivalent = compacted.end();
            if (entry.overwrote_existing) {
                fs::path const entry_path(entry.absolute_output_path);
                std::string entry_name = entry_path.filename().string();
                std::transform(entry_name.begin(), entry_name.end(), entry_name.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                equivalent = std::find_if(compacted.begin(), compacted.end(), [&](auto const& prior) {
                    fs::path const prior_path(prior.absolute_output_path);
                    if (prior_path.parent_path().lexically_normal()
                        != entry_path.parent_path().lexically_normal()) {
                        return false;
                    }
                    std::string prior_name = prior_path.filename().string();
                    std::transform(prior_name.begin(), prior_name.end(), prior_name.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    return prior_name == entry_name;
                });
            }
            if (equivalent == compacted.end()) {
                compacted.push_back(std::move(entry));
            } else {
                *equivalent = std::move(entry);
            }
        }
        return compacted;
    }

    std::vector<ExtractRollbackEntry> NativeExtractCallback::take_rollback_entries() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (ExtractRollbackEntry& entry : rollback_entries_) {
            if (!entry.restore_directory_metadata || !entry.output_identity.defined) {
                continue;
            }
            std::string const key = std::to_string(entry.output_identity.volume) + ":"
                                  + std::to_string(entry.output_identity.object);
            auto const original = directory_original_metadata_.find(key);
            if (original != directory_original_metadata_.end()) {
                entry.original_permissions = original->second.permissions;
                entry.original_mtime = original->second.mtime;
                entry.original_mtime_defined = original->second.mtime_defined;
            }
        }
        return std::move(rollback_entries_);
    }

    bool NativeExtractCallback::budget_triggered() const {
        return budget_tracker_ != nullptr && budget_tracker_->triggered();
    }

    std::string NativeExtractCallback::budget_trigger_reason() const {
        return budget_tracker_ != nullptr ? budget_tracker_->trigger_reason() : std::string{};
    }

    BudgetExceededAction NativeExtractCallback::budget_policy() const {
        return budget_tracker_ != nullptr ? budget_tracker_->policy() : BudgetExceededAction::kFailAndRollback;
    }

    bool NativeExtractCallback::request_selects_single_logical_root() const {
        if (selected_entries_.empty()) {
            return true;
        }
        return selected_entries_.size() == 1;
    }

} // namespace z7::app
