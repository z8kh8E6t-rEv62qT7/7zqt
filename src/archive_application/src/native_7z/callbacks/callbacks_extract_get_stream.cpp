// src/archive_application/src/native_7z/callbacks/callbacks_extract_get_stream.cpp
// Role: Extract callback output stream selection and materialization setup.

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {
    namespace {

        bool output_path_exists(fs::path const& path, std::error_code& ec) {
            ec.clear();
            fs::file_status const status = fs::symlink_status(path, ec);
            if (ec == std::errc::no_such_file_or_directory) {
                ec.clear();
                return false;
            }
            if (ec) {
                return false;
            }
            return fs::status_known(status) && status.type() != fs::file_type::not_found;
        }

        fs::path make_unique_destination_path_no_follow(fs::path const& original_path, std::error_code& ec) {
            ec.clear();
            std::error_code exists_ec;
            if (!output_path_exists(original_path, exists_ec)) {
                if (exists_ec) {
                    ec = exists_ec;
                    return {};
                }
                return original_path;
            }
            if (exists_ec) {
                ec = exists_ec;
                return {};
            }

            fs::path const parent = original_path.parent_path();
            std::string const stem = original_path.stem().string();
            std::string const ext = original_path.extension().string();
            for (uint64_t suffix = 1; suffix < 10000; ++suffix) {
                fs::path const candidate = parent / fs::path(stem + "_" + std::to_string(suffix) + ext);
                exists_ec.clear();
                if (!output_path_exists(candidate, exists_ec)) {
                    if (exists_ec) {
                        ec = exists_ec;
                        return {};
                    }
                    return candidate;
                }
                if (exists_ec) {
                    ec = exists_ec;
                    return {};
                }
            }

            uint64_t const unique_suffix =
                static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            fs::path const candidate = parent / fs::path(stem + "_" + std::to_string(unique_suffix) + ext);
            exists_ec.clear();
            if (!output_path_exists(candidate, exists_ec)) {
                if (exists_ec) {
                    ec = exists_ec;
                    return {};
                }
                return candidate;
            }
            if (exists_ec) {
                ec = exists_ec;
            } else {
                ec = std::make_error_code(std::errc::file_exists);
            }
            return {};
        }

        bool remove_existing_output_for_overwrite(fs::path const& destination_path, std::error_code& ec) {
            ec.clear();
            std::error_code exists_ec;
            if (!output_path_exists(destination_path, exists_ec)) {
                if (exists_ec) {
                    ec = exists_ec;
                    return false;
                }
                return true;
            }
            return fs::remove(destination_path, ec);
        }

    } // namespace

    bool NativeExtractCallback::create_output_directories_with_zone_identifier(fs::path const& directory_path,
                                                                               std::error_code& ec) const {
        ec.clear();
        if (directory_path.empty()) {
            return true;
        }

        std::vector<fs::path> created_candidates;
        for (fs::path cursor = directory_path; !cursor.empty();) {
            std::error_code status_ec;
            fs::file_status const status = fs::symlink_status(cursor, status_ec);
            if (!status_ec && fs::status_known(status) && status.type() != fs::file_type::not_found) {
                break;
            }
            if (status_ec && status_ec != std::errc::no_such_file_or_directory) {
                ec = status_ec;
                return false;
            }

            created_candidates.push_back(cursor);
            fs::path const parent = cursor.parent_path();
            if (parent.empty() || parent == cursor) {
                break;
            }
            cursor = parent;
        }

        fs::create_directories(directory_path, ec);
        if (ec) {
            return false;
        }

        for (auto it = created_candidates.rbegin(); it != created_candidates.rend(); ++it) {
            std::error_code status_ec;
            fs::file_status const status = fs::symlink_status(*it, status_ec);
            if (!status_ec && fs::is_directory(status)) {
                apply_zone_identifier_to_output(*it, true);
            }
        }
        return true;
    }

    bool NativeExtractCallback::try_reserve_declared_budget_bytes(uint64_t declared_size) {
        if (!budget_.has_value() || !budget_->max_bytes.has_value()) {
            return true;
        }

        uint64_t const limit = *budget_->max_bytes;
        uint64_t current = budget_bytes_seen_.load(std::memory_order_acquire);
        while (true) {
            if (current > limit || declared_size > limit - current) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!budget_triggered_.exchange(true, std::memory_order_acq_rel)) {
                    budget_trigger_reason_ = "max_bytes limit exceeded (" + std::to_string(limit) + ")";
                }
                return false;
            }
            if (budget_bytes_seen_.compare_exchange_weak(
                    current, current + declared_size, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return true;
            }
        }
    }

    void NativeExtractCallback::release_reserved_budget_bytes(uint64_t declared_size) {
        if (declared_size != 0) {
            budget_bytes_seen_.fetch_sub(declared_size, std::memory_order_acq_rel);
        }
    }

    bool NativeExtractCallback::ensure_output_path_is_authorized(fs::path const& path_to_resolve,
                                                                 fs::path const& authorized_root,
                                                                 fs::path const& reported_output_path) {
        std::error_code ec;
        if (path_is_within_authorized_root(path_to_resolve, authorized_root, ec)) {
            return true;
        }
        record_io_error("Output path resolves outside authorized extraction root: "
                        + reported_output_path.generic_string()
                        + (ec ? std::string("; ") + ec.message() : ""));
        return false;
    }

    HRESULT NativeExtractCallback::prepare_output_target(UInt32 index,
                                                         std::string const& output_item_path,
                                                         ResolvedPath const& resolved_path,
                                                         OutputTarget& target,
                                                         bool& skipped) {
        skipped = false;
        target = OutputTarget{};
        target.archive_entry_path = output_item_path;
        target.destination_path = resolved_path.destination_path;
        target.output_path = resolved_path.destination_path;
        target.authorized_root = resolved_path.authorized_root;
        target.absolute_output_path = resolved_path.absolute_output_path;

        if (!ensure_output_path_is_authorized(
                target.destination_path.parent_path(), target.authorized_root, target.destination_path)) {
            return E_FAIL;
        }

        std::error_code ec;
        bool const exists = output_path_exists(target.destination_path, ec);
        if (ec) {
            record_io_error("Cannot query output path: " + target.destination_path.generic_string());
            return E_FAIL;
        }

        bool should_remove_existing = false;
        if (exists) {
            switch (overwrite_mode_) {
                case OverwriteMode::kSkip:
                    {
                        emit_log_event(hooks_,
                                       OperationStage::kRunning,
                                       OutputChannel::kNone,
                                       "Skipping existing file: " + target.destination_path.generic_string());
                        skipped = true;
                        return S_OK;
                    }
                case OverwriteMode::kAsk:
                    {
                        if (ask_yes_to_all_) {
                            target.had_original = true;
                            should_remove_existing = true;
                            break;
                        }
                        if (ask_no_to_all_) {
                            emit_log_event(hooks_,
                                           OperationStage::kRunning,
                                           OutputChannel::kNone,
                                           "Skipping existing file: " + target.destination_path.generic_string());
                            skipped = true;
                            return S_OK;
                        }

                        OverwriteDecision const decision =
                            ask_overwrite_decision(target.destination_path, index, output_item_path);
                        switch (decision) {
                            case OverwriteDecision::kYes:
                                {
                                    target.had_original = true;
                                    should_remove_existing = true;
                                    break;
                                }
                            case OverwriteDecision::kYesToAll:
                                {
                                    ask_yes_to_all_ = true;
                                    target.had_original = true;
                                    should_remove_existing = true;
                                    break;
                                }
                            case OverwriteDecision::kNo:
                                {
                                    emit_log_event(hooks_,
                                                   OperationStage::kRunning,
                                                   OutputChannel::kNone,
                                                   "Skipping existing file: "
                                                       + target.destination_path.generic_string());
                                    skipped = true;
                                    return S_OK;
                                }
                            case OverwriteDecision::kNoToAll:
                                {
                                    ask_no_to_all_ = true;
                                    emit_log_event(hooks_,
                                                   OperationStage::kRunning,
                                                   OutputChannel::kNone,
                                                   "Skipping existing file: "
                                                       + target.destination_path.generic_string());
                                    skipped = true;
                                    return S_OK;
                                }
                            case OverwriteDecision::kAutoRename:
                                {
                                    std::error_code unique_ec;
                                    target.output_path =
                                        make_unique_destination_path_no_follow(target.destination_path, unique_ec);
                                    if (unique_ec || target.output_path.empty()) {
                                        record_io_error("Cannot allocate renamed output path: "
                                                        + target.destination_path.generic_string()
                                                        + (unique_ec ? std::string("; ") + unique_ec.message() : ""));
                                        return E_FAIL;
                                    }
                                    break;
                                }
                            case OverwriteDecision::kCancel:
                                {
                                    return E_ABORT;
                                }
                        }
                        break;
                    }
                case OverwriteMode::kOverwrite:
                    {
                        target.had_original = true;
                        should_remove_existing = true;
                        break;
                    }
                case OverwriteMode::kRenameExisting:
                    {
                        std::error_code unique_ec;
                        fs::path const renamed_existing =
                            make_unique_destination_path_no_follow(target.destination_path, unique_ec);
                        if (unique_ec || renamed_existing.empty()) {
                            record_io_error("Cannot allocate renamed destination path: "
                                            + target.destination_path.generic_string()
                                            + (unique_ec ? std::string("; ") + unique_ec.message() : ""));
                            return E_FAIL;
                        }
                        fs::rename(target.destination_path, renamed_existing, ec);
                        if (ec) {
                            record_io_error("Cannot rename existing destination: "
                                            + target.destination_path.generic_string());
                            return E_FAIL;
                        }
                        target.backup_path = renamed_existing;
                        target.had_original = true;
                        target.preserve_backup_on_commit = true;
                        emit_log_event(hooks_,
                                       OperationStage::kRunning,
                                       OutputChannel::kNone,
                                       "Renamed existing file to: " + renamed_existing.generic_string());
                        break;
                    }
                case OverwriteMode::kRenameExtracted:
                    {
                        std::error_code unique_ec;
                        target.output_path = make_unique_destination_path_no_follow(target.destination_path, unique_ec);
                        if (unique_ec || target.output_path.empty()) {
                            record_io_error("Cannot allocate renamed output path: "
                                            + target.destination_path.generic_string()
                                            + (unique_ec ? std::string("; ") + unique_ec.message() : ""));
                            return E_FAIL;
                        }
                        break;
                    }
            }
        }

        if (should_remove_existing && !remove_existing_output_for_overwrite(target.output_path, ec)) {
            record_io_error("Cannot delete output path before overwrite: "
                            + target.output_path.generic_string()
                            + (ec ? std::string("; ") + ec.message() : ""));
            return E_FAIL;
        }

        if (!create_output_directories_with_zone_identifier(target.output_path.parent_path(), ec)) {
            record_io_error("Cannot create output directory: " + target.output_path.parent_path().generic_string());
            return E_FAIL;
        }
        if (!ensure_output_path_is_authorized(
                target.output_path.parent_path(), target.authorized_root, target.output_path)) {
            return E_FAIL;
        }

        target.overwrote_existing = exists && (target.output_path == target.destination_path);
        target.renamed_from_collision = (target.output_path != target.destination_path);
        if (target.renamed_from_collision) {
            target.absolute_output_path = fs::absolute(target.output_path).generic_string();
        }
        return S_OK;
    }

    STDMETHODIMP
    NativeExtractCallback::GetStream(UInt32 index, ISequentialOutStream** out_stream, Int32 ask_extract_mode) throw() {
        if (out_stream == nullptr) {
            return E_INVALIDARG;
        }
        *out_stream = nullptr;

        std::string archive_entry_path = normalize_archive_item_path(archive_get_prop_text(archive_, index, kpidPath));
        if (!archive_entry_path.empty() && !archive_virtual_path_is_safe_for_materialization(archive_entry_path)) {
            record_io_error("Unsafe archive entry path escapes destination: " + archive_entry_path);
            return E_FAIL;
        }

        bool is_dir = false;
        (void)archive_get_prop_bool(archive_, index, kpidIsDir, is_dir);
        bool is_encrypted = false;
        (void)archive_get_prop_bool(archive_, index, kpidEncrypted, is_encrypted);
        std::string const display_path = archive_entry_path.empty() ? std::to_string(index) : archive_entry_path;
        std::string const output_item_path = archive_entry_path.empty() && !is_dir ? display_path : archive_entry_path;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_path_ = display_path;
            current_item_encrypted_ = is_encrypted;
        }
        emit_progress_snapshot();

        const HRESULT canceled_check = check_canceled();
        if (canceled_check != S_OK) {
            return canceled_check;
        }

        if (restore_file_security_ && !security_notice_emitted_) {
            security_notice_emitted_ = true;
            emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kNone, "Restore file security is enabled.");
        }

        if (ask_extract_mode != NArchive::NExtract::NAskMode::kExtract) {
            return S_OK;
        }

        // Budget check: count files (dirs + regular files each count as one entry).
        if (budget_.has_value() && budget_->max_files.has_value()) {
            uint64_t const seen = budget_files_seen_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (seen > *budget_->max_files) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!budget_triggered_.exchange(true, std::memory_order_acq_rel)) {
                    budget_trigger_reason_ = "max_files limit exceeded (" + std::to_string(*budget_->max_files) + ")";
                }
                return E_ABORT;
            }
        }

        if (buffer_sink_ != nullptr) {
            if (is_dir) {
                return S_OK;
            }
            auto* stream = new NativeBufferOutStream(*buffer_sink_, buffer_sink_max_size_);
            *out_stream = stream;
            return S_OK;
        }

        ResolvedPath const resolved_path = resolve_destination_path(output_item_path, is_dir);
        fs::path const destination_path = resolved_path.destination_path;
        ExtractItemAttributes item_attributes;
        const HRESULT attributes_res = read_item_attributes(index, item_attributes);
        if (attributes_res != S_OK) {
            record_io_error("Cannot read output file attributes: " + display_path);
            return attributes_res;
        }
        ExtractItemLinkInfo item_link_info;
        const HRESULT link_res = read_item_link_info(index, item_link_info);
        if (link_res != S_OK) {
            record_io_error("Cannot read output link target: " + display_path);
            return link_res;
        }

        std::error_code ec;
        if (is_dir) {
            if (!ensure_output_path_is_authorized(destination_path, resolved_path.authorized_root, destination_path)) {
                return E_FAIL;
            }
            bool const existed_before = fs::exists(destination_path, ec);
            if (ec) {
                record_io_error("Cannot query output path: " + destination_path.generic_string());
                return E_FAIL;
            }
            if (!create_output_directories_with_zone_identifier(destination_path, ec)) {
                record_io_error("Cannot create output directory: " + destination_path.generic_string());
                return E_FAIL;
            }
            if (!ensure_output_path_is_authorized(destination_path, resolved_path.authorized_root, destination_path)) {
                return E_FAIL;
            }
            if (std::optional<std::string> const warning = apply_item_attributes(destination_path, item_attributes);
                warning.has_value()) {
                record_nonfatal_warning(*warning);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ExtractMaterializedEntry dir_entry;
                dir_entry.archive_entry_path = output_item_path;
                dir_entry.absolute_output_path = resolved_path.absolute_output_path;
                dir_entry.is_directory = true;
                materialized_entries_.push_back(std::move(dir_entry));
                if (!existed_before) {
                    ExtractRollbackEntry rollback_entry;
                    rollback_entry.output_path = destination_path;
                    rollback_entry.destination_path = destination_path;
                    rollback_entry.is_directory = true;
                    rollback_entries_.push_back(std::move(rollback_entry));
                }
            }
            return S_OK;
        }

        if (item_link_info.is_link()) {
            OutputTarget target;
            target.archive_entry_path = output_item_path;
            target.destination_path = destination_path;
            target.output_path = destination_path;
            target.authorized_root = resolved_path.authorized_root;
            target.absolute_output_path = resolved_path.absolute_output_path;

            LinkCreationPlan link_plan;
            if (std::optional<std::string> const warning =
                    prepare_link_creation_plan(target, item_link_info, link_plan);
                warning.has_value()) {
                record_nonfatal_warning(*warning);
                return S_OK;
            }

            bool skipped = false;
            const HRESULT target_res = prepare_output_target(index, output_item_path, resolved_path, target, skipped);
            if (target_res != S_OK || skipped) {
                return target_res;
            }

            if (std::optional<std::string> const warning = materialize_link(target, link_plan, true);
                warning.has_value()) {
                record_nonfatal_warning(*warning);
            }
            return S_OK;
        }

        uint64_t declared_size = 0;
        bool const declared_size_defined = archive_get_prop_uint64(archive_, index, kpidSize, declared_size);
        bool const reserve_budget_bytes =
            declared_size_defined && budget_.has_value() && budget_->max_bytes.has_value();
        if (reserve_budget_bytes && !try_reserve_declared_budget_bytes(declared_size)) {
            return E_ABORT;
        }

        OutputTarget target;
        bool skipped = false;
        const HRESULT target_res = prepare_output_target(index, output_item_path, resolved_path, target, skipped);
        if (target_res != S_OK || skipped) {
            if (reserve_budget_bytes) {
                release_reserved_budget_bytes(declared_size);
            }
            return target_res;
        }

        auto* stream = new NativeFileOutStream(target.output_path);
        const HRESULT open_res = stream->open();
        if (open_res != S_OK) {
            record_io_error("Cannot create output file: " + target.output_path.generic_string());
            stream->Release();
            if (reserve_budget_bytes) {
                release_reserved_budget_bytes(declared_size);
            }
            return open_res;
        }

        stream->AddRef();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            PendingEntry pe;
            pe.archive_entry_path = target.archive_entry_path;
            pe.absolute_output_path = target.absolute_output_path;
            pe.output_path = target.output_path;
            pe.destination_path = target.destination_path;
            pe.backup_path = target.backup_path;
            pe.had_original = target.had_original;
            pe.overwrote_existing = target.overwrote_existing;
            pe.renamed_from_collision = target.renamed_from_collision;
            pe.preserve_backup_on_commit = target.preserve_backup_on_commit;
            pe.declared_size = declared_size_defined ? declared_size : 0;
            pe.budget_bytes_reserved = reserve_budget_bytes;
            pe.attributes = item_attributes;
            pe.owned_stream = stream;
            pending_entry_ = std::move(pe);
        }

        *out_stream = stream;
        return S_OK;
    }

} // namespace z7::app
