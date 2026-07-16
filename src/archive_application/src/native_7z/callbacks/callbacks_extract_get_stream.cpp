// src/archive_application/src/native_7z/callbacks/callbacks_extract_get_stream.cpp
// Role: Extract callback output stream selection and materialization setup.

#include "core/filesystem_replace.h"
#include "core/internal.h"
#include "operations/extract_output.h"
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

#if defined(__APPLE__)
        std::optional<std::string> rejected_archive_xattr_reason(std::string const& name) {
            if (name.empty() || name.find('\0') != std::string::npos) {
                return "attribute name is empty or contains a NUL byte";
            }

            static constexpr std::string_view kDeniedNames[] = {
                "com.apple.quarantine",
                "com.apple.provenance",
                "com.apple.macl",
                "com.apple.rootless",
                "com.apple.system.Security",
                "com.apple.decmpfs",
            };
            for (std::string_view denied : kDeniedNames) {
                if (name == denied) {
                    return "security or filesystem-control metadata is governed by extraction policy";
                }
            }
            static constexpr std::string_view kDeniedPrefixes[] = {
                "com.apple.acl.",
                "com.apple.security.",
            };
            for (std::string_view denied : kDeniedPrefixes) {
                if (name.compare(0, denied.size(), denied) == 0) {
                    return "security metadata is governed by extraction policy";
                }
            }
            return std::nullopt;
        }
#endif

    } // namespace

    bool NativeExtractCallback::create_output_directories_with_zone_identifier(fs::path const& directory_path,
                                                                               std::error_code& ec) {
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

        for (auto it = created_candidates.rbegin(); it != created_candidates.rend(); ++it) {
            bool const created = fs::create_directory(*it, ec);
            if (ec) {
                return false;
            }
            if (!created) {
                std::error_code status_ec;
                if (!fs::is_directory(fs::symlink_status(*it, status_ec)) || status_ec) {
                    ec = status_ec ? status_ec : std::make_error_code(std::errc::not_a_directory);
                    return false;
                }
                continue;
            }
            apply_zone_identifier_to_output(*it, true);

            ExtractRollbackEntry rollback_entry;
            rollback_entry.output_path = *it;
            rollback_entry.destination_path = *it;
            rollback_entry.is_directory = true;
            rollback_entry.remove_only_if_empty = true;
            std::error_code identity_ec;
            rollback_entry.output_identity = capture_filesystem_object_identity_no_follow(*it, identity_ec);
            std::lock_guard<std::mutex> lock(mutex_);
            rollback_entries_.push_back(std::move(rollback_entry));
        }

        // Inserting a child changes the parent directory's mtime. Snapshot the
        // preexisting parent before the child is committed; a later explicit
        // archive directory entry is already too late for rollback purposes.
        if (std::find(created_candidates.begin(), created_candidates.end(), directory_path)
            == created_candidates.end()) {
            std::error_code identity_ec;
            FilesystemObjectIdentity const identity =
                capture_filesystem_object_identity_no_follow(directory_path, identity_ec);
            if (identity_ec || !identity.defined) {
                ec = identity_ec ? identity_ec : std::make_error_code(std::errc::state_not_recoverable);
                return false;
            }
            fs::file_status const status = fs::status(directory_path, ec);
            if (ec || !fs::is_directory(status)) {
                return false;
            }
            fs::file_time_type const mtime = fs::last_write_time(directory_path, ec);
            if (ec) {
                return false;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            bool const created_by_request = std::any_of(
                rollback_entries_.begin(), rollback_entries_.end(), [&](ExtractRollbackEntry const& entry) {
                    return entry.is_directory && !entry.restore_directory_metadata && !entry.had_original
                        && entry.output_identity.defined && entry.output_identity.volume == identity.volume
                        && entry.output_identity.object == identity.object;
                });
            if (created_by_request) {
                return true;
            }
            std::string const identity_key = std::to_string(identity.volume) + ":" + std::to_string(identity.object);
            directory_original_metadata_.try_emplace(
                identity_key,
                DirectoryOriginalMetadata{.permissions = status.permissions(), .mtime = mtime, .mtime_defined = true});
            bool const already_snapshotted = std::any_of(
                rollback_entries_.begin(), rollback_entries_.end(), [&](ExtractRollbackEntry const& entry) {
                    return entry.restore_directory_metadata && entry.output_identity.defined
                        && entry.output_identity.volume == identity.volume
                        && entry.output_identity.object == identity.object;
                });
            if (!already_snapshotted) {
                ExtractRollbackEntry snapshot;
                snapshot.output_path = directory_path;
                snapshot.destination_path = directory_path;
                snapshot.is_directory = true;
                snapshot.restore_directory_metadata = true;
                snapshot.original_permissions = status.permissions();
                snapshot.original_mtime = mtime;
                snapshot.original_mtime_defined = true;
                snapshot.output_identity = identity;
                rollback_entries_.push_back(std::move(snapshot));
            }
        }
        return true;
    }

    bool NativeExtractCallback::try_reserve_budget_file() {
        return budget_tracker_ == nullptr || budget_tracker_->try_reserve_file();
    }

    void NativeExtractCallback::release_budget_file() {
        if (budget_tracker_ != nullptr) {
            budget_tracker_->release_file();
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
                                                         bool is_directory,
                                                         OutputTarget& target,
                                                         bool& skipped) {
        skipped = false;
        target = OutputTarget{};
        target.archive_index = index;
        target.archive_entry_path = output_item_path;
        target.destination_path = resolved_path.destination_path;
        target.output_path = resolved_path.destination_path;
        target.authorized_root = resolved_path.authorized_root;
        target.absolute_output_path = resolved_path.absolute_output_path;

        bool const maps_directory_to_authorized_root =
            is_directory && target.destination_path.lexically_normal() == target.authorized_root.lexically_normal();
        fs::path const initial_authorization_probe =
            maps_directory_to_authorized_root ? target.destination_path : target.destination_path.parent_path();
        if (!ensure_output_path_is_authorized(
                initial_authorization_probe, target.authorized_root, target.destination_path)) {
            return E_FAIL;
        }

        std::error_code ec;
        bool const exists = output_path_exists(target.destination_path, ec);
        if (ec) {
            record_io_error("Cannot query output path: " + target.destination_path.generic_string());
            return E_FAIL;
        }
        if (exists) {
            target.original_identity = capture_filesystem_object_identity_no_follow(target.destination_path, ec);
            if (ec || !target.original_identity.defined) {
                record_io_error("Cannot identify existing output path: "
                                + target.destination_path.generic_string()
                                + (ec ? std::string("; ") + ec.message() : ""));
                return E_FAIL;
            }
            if (std::optional<std::string> const collision =
                    materialized_collision_archive_entry(target.destination_path);
                collision.has_value()) {
                target.collided_archive_entry_path = *collision;
            }
        }

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
                                    break;
                                }
                            case OverwriteDecision::kYesToAll:
                                {
                                    ask_yes_to_all_ = true;
                                    target.had_original = true;
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
                        target.backup_path = renamed_existing;
                        target.had_original = true;
                        target.preserve_backup_on_commit = true;
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

        if (check_canceled() != S_OK) {
            return E_ABORT;
        }

        if (!create_output_directories_with_zone_identifier(target.output_path.parent_path(), ec)) {
            record_io_error("Cannot create output directory: " + target.output_path.parent_path().generic_string());
            return E_FAIL;
        }
        fs::path authorization_probe = target.output_path.parent_path();
        fs::path authorization_root = target.authorized_root;
        bool const renamed_authorized_root =
            maps_directory_to_authorized_root && target.output_path != target.destination_path;
        if (maps_directory_to_authorized_root && !renamed_authorized_root) {
            authorization_probe = target.output_path;
        } else if (renamed_authorized_root) {
            authorization_root = target.authorized_root.parent_path();
        }
        if (!ensure_output_path_is_authorized(authorization_probe, authorization_root, target.output_path)) {
            return E_FAIL;
        }
        if (renamed_authorized_root) {
            // Descendants must be confined to the newly selected directory,
            // not to the broader parent used to authorize its sibling name.
            target.authorized_root = target.output_path;
        }

        std::unique_ptr<FilesystemTransaction> transaction =
            FilesystemTransaction::create(target.output_path, "extract", ec);
        if (!transaction) {
            record_io_error("Cannot create private extraction transaction: "
                            + target.output_path.parent_path().generic_string()
                            + (ec ? std::string("; ") + ec.message() : ""));
            return E_FAIL;
        }
        target.transaction = std::shared_ptr<FilesystemTransaction>(std::move(transaction));
        target.temp_path = target.transaction->allocate_path("output");

        target.overwrote_existing = exists && (target.output_path == target.destination_path);
        target.renamed_from_collision = (target.output_path != target.destination_path);
        if (target.renamed_from_collision) {
            std::error_code absolute_ec;
            fs::path const absolute_path = fs::absolute(target.output_path, absolute_ec);
            if (!absolute_ec) {
                std::error_code canonical_ec;
                fs::path const canonical_path = fs::weakly_canonical(absolute_path, canonical_ec);
                target.absolute_output_path =
                    (canonical_ec ? absolute_path.lexically_normal() : canonical_path).generic_string();
            }
        }
        return S_OK;
    }

    STDMETHODIMP
    NativeExtractCallback::GetStream(UInt32 index, ISequentialOutStream** out_stream, Int32 ask_extract_mode) throw() {
        if (out_stream == nullptr) {
            return E_INVALIDARG;
        }
        *out_stream = nullptr;

        const HRESULT prior_item_result = finalize_unreported_item_if_needed();
        if (prior_item_result != S_OK) {
            return prior_item_result;
        }

        ArchiveItemPath item_path;
        HRESULT const path_result = resolve_archive_item_path(arc_, index, item_path);
        if (path_result != S_OK) {
            record_io_error("Cannot resolve archive entry path for item " + std::to_string(index));
            return path_result;
        }
        std::string const& archive_entry_path = item_path.normalized;
        if (!archive_entry_path.empty() && !archive_virtual_path_is_safe_for_materialization(archive_entry_path)) {
            record_io_error("Unsafe archive entry path escapes destination: " + archive_entry_path);
            return E_FAIL;
        }
        std::string invalid_path_reason;
        if (!validate_output_item_path(item_path.resolved, invalid_path_reason)) {
            record_io_error(
                "Unsafe archive entry path was rejected: " + item_path.resolved + "; " + invalid_path_reason);
            return E_FAIL;
        }

        bool is_dir = false;
        (void)archive_get_prop_bool(archive_, index, kpidIsDir, is_dir);
        bool is_anti = false;
        (void)archive_get_prop_bool(archive_, index, kpidIsAnti, is_anti);
        bool is_encrypted = false;
        (void)archive_get_prop_bool(archive_, index, kpidEncrypted, is_encrypted);
        ExtractItemAlternateStreamInfo alternate_stream_info;
        const HRESULT alternate_stream_result = read_item_alternate_stream_info(index, alternate_stream_info);
        if (alternate_stream_result != S_OK) {
            record_io_error("Cannot read alternate-stream metadata: " + archive_entry_path);
            return alternate_stream_result;
        }
        std::string const display_path = archive_entry_path;
        std::string const output_item_path = archive_entry_path;

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

        {
            NWindows::NCOM::CPropVariant position;
            HRESULT const position_result = archive_->GetProperty(index, kpidPosition, &position);
            if (position_result != S_OK) {
                record_io_error("Cannot read split archive entry position: " + display_path);
                return position_result;
            }
            if (position.vt != VT_EMPTY) {
                if (position.vt != VT_UI8) {
                    record_io_error("Invalid split archive entry position: " + display_path);
                    return E_FAIL;
                }
                remember_skipped_archive_item(index);
                record_partial_warning("Split archive entry is unsupported and was skipped: " + display_path);
                return S_OK;
            }
        }

        // Several filesystem/container formats expose their logical root as an
        // explicit directory item named ".". It is metadata for the archive
        // namespace, not an instruction to replace the caller-owned output
        // directory's permissions or timestamps.
        if (is_dir && archive_entry_path.empty()) {
            return S_OK;
        }

        if (is_anti) {
            emit_log_event(hooks_,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Ignoring anti-item during extraction: " + display_path);
            return S_OK;
        }

        for (std::string const& skipped_prefix : skipped_directory_prefixes_) {
            if (output_item_path == skipped_prefix
                || (output_item_path.size() > skipped_prefix.size()
                    && output_item_path.compare(0, skipped_prefix.size(), skipped_prefix) == 0
                    && output_item_path[skipped_prefix.size()] == '/')) {
                emit_log_event(hooks_,
                               OperationStage::kRunning,
                               OutputChannel::kNone,
                               "Skipping entry below skipped directory: " + display_path);
                return S_OK;
            }
        }

#if defined(__APPLE__)
        if (alternate_stream_info.is_alternate_stream) {
            if (buffer_sink_ != nullptr) {
                return S_OK;
            }
            MaterializedOutputTarget parent_target;
            bool parent_was_skipped = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto const parent = materialized_output_targets_.find(alternate_stream_info.parent_index);
                if (parent != materialized_output_targets_.end()) {
                    parent_target = parent->second;
                } else {
                    parent_was_skipped = skipped_archive_indices_.contains(alternate_stream_info.parent_index);
                }
            }
            if (parent_target.output_path.empty()) {
                if (!parent_was_skipped) {
                    record_partial_warning("Alternate stream parent was not materialized and was skipped: "
                                           + display_path);
                }
                return S_OK;
            }
            if (std::optional<std::string> const rejection =
                    rejected_archive_xattr_reason(alternate_stream_info.attribute_name);
                rejection.has_value()) {
                record_partial_warning("Archived extended attribute was rejected by policy: "
                                       + display_path
                                       + " ("
                                       + *rejection
                                       + ")");
                return S_OK;
            }

            std::error_code identity_ec;
            if (!filesystem_object_matches_identity_no_follow(
                    parent_target.output_path, parent_target.identity, identity_ec)) {
                record_partial_warning("Alternate stream parent changed before metadata restoration: "
                                       + parent_target.output_path.generic_string()
                                       + (identity_ec ? std::string("; ") + identity_ec.message() : ""));
                return S_OK;
            }
            std::error_code authorization_ec;
            if (!path_is_within_authorized_root(
                    parent_target.output_path, parent_target.authorized_root, authorization_ec)) {
                record_partial_warning("Alternate stream parent resolves outside the authorized extraction root: "
                                       + parent_target.output_path.generic_string()
                                       + (authorization_ec ? std::string("; ") + authorization_ec.message() : ""));
                return S_OK;
            }

            std::error_code transaction_ec;
            std::unique_ptr<FilesystemTransaction> transaction =
                FilesystemTransaction::create(parent_target.output_path, "xattr", transaction_ec);
            if (!transaction) {
                record_partial_warning("Cannot create private transaction for extended attribute: "
                                       + display_path
                                       + (transaction_ec ? std::string("; ") + transaction_ec.message() : ""));
                return S_OK;
            }
            std::shared_ptr<FilesystemTransaction> shared_transaction(std::move(transaction));
            fs::path const temp_path = shared_transaction->allocate_path("xattr");
            auto* stream = new NativeFileOutStream(temp_path, budget_tracker_);
            const HRESULT open_result = stream->open();
            if (open_result != S_OK) {
                std::string const failure = stream->failure_message();
                stream->Release();
                std::string ignored;
                (void)shared_transaction->finish(&ignored);
                record_partial_warning(failure.empty()
                                           ? "Cannot stage extended attribute: " + display_path
                                           : failure);
                return S_OK;
            }

            std::error_code temp_identity_ec;
            FilesystemObjectIdentity const temp_identity =
                capture_filesystem_object_identity_no_follow(temp_path, temp_identity_ec);
            if (temp_identity_ec || !temp_identity.defined) {
                (void)stream->Close();
                stream->Release();
                (void)shared_transaction->discard(temp_path);
                std::string ignored;
                (void)shared_transaction->finish(&ignored);
                record_partial_warning("Cannot identify staged extended attribute: "
                                       + display_path
                                       + (temp_identity_ec ? std::string("; ") + temp_identity_ec.message() : ""));
                return S_OK;
            }

            stream->AddRef();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                PendingAlternateStream pending;
                pending.archive_index = index;
                pending.parent_index = alternate_stream_info.parent_index;
                pending.archive_entry_path = display_path;
                pending.attribute_name = alternate_stream_info.attribute_name;
                pending.output_path = parent_target.output_path;
                pending.authorized_root = parent_target.authorized_root;
                pending.output_identity = parent_target.identity;
                pending.parent_times = parent_target.times;
                pending.parent_is_symlink = parent_target.is_symlink;
                pending.temp_path = temp_path;
                pending.temp_identity = temp_identity;
                pending.transaction = std::move(shared_transaction);
                pending.owned_stream = stream;
                pending_alternate_stream_ = std::move(pending);
            }
            *out_stream = stream;
            return S_OK;
        }
#endif

        if (buffer_sink_ != nullptr) {
            if (is_dir) {
                return S_OK;
            }
            auto* stream = new NativeBufferOutStream(*buffer_sink_, buffer_sink_max_size_);
            *out_stream = stream;
            return S_OK;
        }

        std::string const path_for_resolution =
            path_mode_ == ExtractPathMode::kAbsolutePaths && is_absolute_item_path(item_path.resolved)
                ? item_path.resolved
                : output_item_path;
        ResolvedPath const resolved_path = resolve_destination_path(path_for_resolution, is_dir);
        fs::path const destination_path = resolved_path.destination_path;
        ExtractItemAttributes item_attributes;
        const HRESULT attributes_res = read_item_attributes(index, item_attributes);
        if (attributes_res != S_OK) {
            record_io_error("Cannot read output file attributes: " + display_path);
            return attributes_res;
        }
        ExtractItemTimes item_times;
        const HRESULT times_res = read_item_times(index, item_times);
        if (times_res != S_OK) {
            record_io_error("Cannot read output file times: " + display_path);
            return times_res;
        }

        if (item_attributes.defined && (item_attributes.attrib & FILE_ATTRIBUTE_UNIX_EXTENSION) != 0) {
            UInt32 const mode = item_attributes.attrib >> 16;
            if (MY_LIN_S_ISFIFO(mode) || MY_LIN_S_ISCHR(mode) || MY_LIN_S_ISBLK(mode) || MY_LIN_S_ISSOCK(mode)) {
                record_partial_warning("Unsupported special filesystem entry was skipped: " + display_path);
                return S_OK;
            }
        }
        ExtractItemLinkInfo item_link_info;
        const HRESULT link_res = read_item_link_info(index, item_link_info);
        if (link_res != S_OK) {
            record_io_error("Cannot read output link target: " + display_path);
            return link_res;
        }

        bool data_stream_symlink = false;
        if (!is_dir && !item_link_info.is_link() && item_attributes.defined
            && (item_attributes.attrib & FILE_ATTRIBUTE_UNIX_EXTENSION) != 0) {
            data_stream_symlink = MY_LIN_S_ISLNK(item_attributes.attrib >> 16);
        }

        std::optional<fs::path> inode_hard_link_target;
        if (!is_dir && !item_link_info.is_link()) {
            uint64_t inode = 0;
            uint64_t stream_id = std::numeric_limits<uint64_t>::max();
            if (archive_get_prop_uint64(archive_, index, kpidINode, inode)) {
                (void)archive_get_prop_uint64(archive_, index, kpidStreamId, stream_id);
                std::string const key = std::to_string(stream_id) + ':' + std::to_string(inode);
                auto source_it = inode_hard_link_source_indices_.find(key);
                if (source_it == inode_hard_link_source_indices_.end()) {
                    UInt32 source_index = index;
                    bool source_has_data = false;
                    UInt32 item_count = 0;
                    if (archive_->GetNumberOfItems(&item_count) == S_OK) {
                        for (UInt32 candidate = 0; candidate < item_count; ++candidate) {
                            bool candidate_is_dir = false;
                            (void)archive_get_prop_bool(archive_, candidate, kpidIsDir, candidate_is_dir);
                            if (candidate_is_dir) {
                                continue;
                            }
                            uint64_t candidate_inode = 0;
                            if (!archive_get_prop_uint64(archive_, candidate, kpidINode, candidate_inode)
                                || candidate_inode != inode) {
                                continue;
                            }
                            uint64_t candidate_stream_id = std::numeric_limits<uint64_t>::max();
                            (void)archive_get_prop_uint64(archive_, candidate, kpidStreamId, candidate_stream_id);
                            if (candidate_stream_id != stream_id) {
                                continue;
                            }
                            uint64_t candidate_size = 0;
                            bool const candidate_has_data =
                                archive_get_prop_uint64(archive_, candidate, kpidSize, candidate_size)
                                && candidate_size != 0;
                            if (!source_has_data || candidate_has_data) {
                                source_index = candidate;
                                source_has_data = candidate_has_data;
                            }
                            if (source_has_data) {
                                break;
                            }
                        }
                    }
                    source_it = inode_hard_link_source_indices_.emplace(key, source_index).first;
                }

                if (source_it->second != index) {
                    std::string const source_archive_path =
                        normalize_archive_item_path(archive_get_prop_text(archive_, source_it->second, kpidPath));
                    bool source_is_selected = selected_entries_.empty();
                    for (std::string const& selected : selected_entries_) {
                        if (source_archive_path == selected
                            || (source_archive_path.size() > selected.size()
                                && source_archive_path.compare(0, selected.size(), selected) == 0
                                && source_archive_path[selected.size()] == '/')) {
                            source_is_selected = true;
                            break;
                        }
                    }
                    if (source_is_selected && !source_archive_path.empty()) {
                        inode_hard_link_target = resolve_destination_path(source_archive_path, false).destination_path;
                        item_link_info.type = ExtractItemLinkInfo::Type::kHardLink;
                    }
                }
            }
        }

        std::error_code ec;
        if (is_dir) {
            if (path_mode_ == ExtractPathMode::kNoPaths) {
                return S_OK;
            }

            OutputTarget target;
            bool skipped = false;
            const HRESULT target_res =
                prepare_output_target(index, output_item_path, resolved_path, true, target, skipped);
            if (target_res != S_OK) {
                return target_res;
            }
            if (skipped) {
                remember_skipped_archive_item(index);
                skipped_directory_prefixes_.push_back(output_item_path);
                return S_OK;
            }
            if (!try_reserve_budget_file()) {
                return E_ABORT;
            }

            if (target.output_path != target.destination_path) {
                directory_destination_remaps_.emplace_back(target.destination_path, target.output_path);
            }

            bool created = false;
            if (target.had_original) {
                if (!filesystem_object_matches_identity_no_follow(target.output_path, target.original_identity, ec)) {
                    release_budget_file();
                    record_io_error("Existing output directory changed during extraction: "
                                    + target.output_path.generic_string()
                                    + (ec ? std::string("; ") + ec.message() : ""));
                    return E_FAIL;
                }
                fs::file_status const existing_status = fs::symlink_status(target.output_path, ec);
                if (ec) {
                    release_budget_file();
                    record_io_error("Cannot query existing output directory: "
                                    + target.output_path.generic_string()
                                    + "; "
                                    + ec.message());
                    return E_FAIL;
                }
                if (target.preserve_backup_on_commit || !fs::is_directory(existing_status)) {
                    fs::path const requested_preserved_path = target.backup_path;
                    TransactionMoveResult const backup =
                        target.transaction->quarantine(target.output_path, &target.original_identity);
                    if (!backup.success) {
                        release_budget_file();
                        record_io_error("Cannot preserve existing output directory: "
                                        + target.output_path.generic_string()
                                        + "; "
                                        + backup.diagnostic);
                        return E_FAIL;
                    }
                    target.backup_path = backup.preserved_path;
                    if (target.preserve_backup_on_commit) {
                        TransactionMoveResult const preserved =
                            target.transaction->restore(target.backup_path, requested_preserved_path);
                        if (!preserved.success) {
                            release_budget_file();
                            record_io_error("Cannot rename existing output directory: " + preserved.diagnostic);
                            return E_FAIL;
                        }
                        target.backup_path = requested_preserved_path;
                    }
                    if (!fs::create_directory(target.output_path, ec)) {
                        if (!target.preserve_backup_on_commit) {
                            (void)target.transaction->restore(target.backup_path, target.output_path);
                        } else {
                            TransactionMoveResult const renamed_backup =
                                target.transaction->quarantine(target.backup_path, &target.original_identity);
                            if (renamed_backup.success) {
                                (void)target.transaction->restore(renamed_backup.preserved_path, target.output_path);
                            }
                        }
                        release_budget_file();
                        record_io_error("Cannot create replacement output directory: "
                                        + target.output_path.generic_string()
                                        + (ec ? std::string("; ") + ec.message() : ""));
                        return E_FAIL;
                    }
                    created = true;
                } else {
                    // The directory must remain writable until all children have
                    // finished, but rollback still needs its original metadata.
                    target.backup_path.clear();
                    target.had_original = false;
                }
            } else {
                if (!fs::create_directory(target.output_path, ec)) {
                    release_budget_file();
                    record_io_error("Cannot create output directory: "
                                    + target.output_path.generic_string()
                                    + (ec ? std::string("; ") + ec.message() : ""));
                    return E_FAIL;
                }
                created = true;
            }

            apply_zone_identifier_to_output(target.output_path, true);
            PendingDirectory pending;
            pending.output_target = std::move(target);
            pending.attributes = item_attributes;
            pending.times = item_times;
            pending.created = created;
            pending.budget_file_reserved = true;
            if (created) {
                std::error_code identity_ec;
                pending.materialized_identity =
                    capture_filesystem_object_identity_no_follow(pending.output_target.output_path, identity_ec);
                if (identity_ec || !pending.materialized_identity.defined) {
                    std::error_code status_ec;
                    bool const output_exists = output_path_exists(pending.output_target.output_path, status_ec);
                    if (!status_ec
                        && !output_exists
                        && pending.output_target.had_original
                        && !pending.output_target.backup_path.empty()
                        && pending.output_target.transaction != nullptr) {
                        fs::path owned_backup = pending.output_target.backup_path;
                        if (owned_backup.parent_path().lexically_normal()
                            != pending.output_target.transaction->directory().lexically_normal()) {
                            TransactionMoveResult const quarantined = pending.output_target.transaction->quarantine(
                                owned_backup, &pending.output_target.original_identity);
                            if (quarantined.success) {
                                owned_backup = quarantined.preserved_path;
                            }
                        }
                        (void)pending.output_target.transaction->restore(
                            owned_backup, pending.output_target.output_path);
                    }
                    release_budget_file();
                    record_io_error("Cannot identify created output directory: "
                                    + pending.output_target.output_path.generic_string()
                                    + (identity_ec ? std::string("; ") + identity_ec.message() : ""));
                    return E_FAIL;
                }
            } else {
                std::error_code metadata_ec;
                pending.original_permissions = fs::status(pending.output_target.output_path, metadata_ec).permissions();
                pending.original_metadata_defined = !metadata_ec;
                metadata_ec.clear();
                pending.original_mtime = fs::last_write_time(pending.output_target.output_path, metadata_ec);
                pending.original_mtime_defined = !metadata_ec;
                std::lock_guard<std::mutex> lock(mutex_);
                bool const created_by_request = std::any_of(
                    rollback_entries_.begin(), rollback_entries_.end(), [&](ExtractRollbackEntry const& entry) {
                        return entry.is_directory && !entry.restore_directory_metadata && !entry.had_original
                            && entry.output_identity.defined && pending.output_target.original_identity.defined
                            && entry.output_identity.volume == pending.output_target.original_identity.volume
                            && entry.output_identity.object == pending.output_target.original_identity.object;
                    });
                if (created_by_request) {
                    pending.original_metadata_defined = false;
                    pending.original_mtime_defined = false;
                }
                std::string const identity_key =
                    std::to_string(pending.output_target.original_identity.volume) + ":"
                    + std::to_string(pending.output_target.original_identity.object);
                auto const original_metadata = directory_original_metadata_.find(identity_key);
                if (!created_by_request && original_metadata != directory_original_metadata_.end()) {
                    pending.original_permissions = original_metadata->second.permissions;
                    pending.original_metadata_defined = true;
                    pending.original_mtime = original_metadata->second.mtime;
                    pending.original_mtime_defined = original_metadata->second.mtime_defined;
                }
                auto const earlier_snapshot = std::find_if(
                    rollback_entries_.begin(), rollback_entries_.end(), [&](ExtractRollbackEntry const& entry) {
                        return entry.restore_directory_metadata && entry.output_identity.defined
                            && pending.output_target.original_identity.defined
                            && entry.output_identity.volume == pending.output_target.original_identity.volume
                            && entry.output_identity.object == pending.output_target.original_identity.object;
                    });
                if (!created_by_request && original_metadata == directory_original_metadata_.end()
                    && earlier_snapshot != rollback_entries_.end()) {
                    pending.original_permissions = earlier_snapshot->original_permissions;
                    pending.original_metadata_defined = true;
                    pending.original_mtime = earlier_snapshot->original_mtime;
                    pending.original_mtime_defined = earlier_snapshot->original_mtime_defined;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_directory_ = std::move(pending);
            }
            return S_OK;
        }

        if (data_stream_symlink) {
            uint64_t declared_size = 0;
            if (!archive_get_prop_uint64(archive_, index, kpidSize, declared_size)
                || declared_size == 0
                || declared_size >= kDataSymlinkLimit) {
                record_partial_warning("Invalid data-stream symbolic link size was skipped: " + display_path);
                return S_OK;
            }
            if (budget_tracker_ != nullptr && !budget_tracker_->can_accept_declared_bytes(declared_size)) {
                return E_ABORT;
            }

            OutputTarget target;
            bool skipped = false;
            const HRESULT target_res =
                prepare_output_target(index, output_item_path, resolved_path, false, target, skipped);
            if (target_res != S_OK || skipped) {
                if (skipped) {
                    remember_skipped_archive_item(index);
                }
                return target_res;
            }
            if (!try_reserve_budget_file()) {
                return E_ABORT;
            }
            if (budget_tracker_ != nullptr && !budget_tracker_->try_reserve_bytes(declared_size)) {
                release_budget_file();
                return E_ABORT;
            }

            NativeBufferOutStream* stream = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                PendingDataSymlink& pending = pending_data_symlink_.emplace();
                pending.output_target = std::move(target);
                pending.attributes = item_attributes;
                pending.times = item_times;
                pending.budget_bytes_reserved = declared_size;
                pending.target_data.reserve(static_cast<size_t>(declared_size));
                stream = new NativeBufferOutStream(pending.target_data, kDataSymlinkLimit - 1);
            }
            *out_stream = stream;
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
            if (inode_hard_link_target.has_value()) {
                link_plan.type = ExtractItemLinkInfo::Type::kHardLink;
                link_plan.hardlink_target_path = *inode_hard_link_target;
            } else {
                if (std::optional<std::string> const warning =
                        prepare_link_creation_plan(target, item_link_info, link_plan);
                    warning.has_value()) {
                    record_partial_warning(*warning);
                    return S_OK;
                }
            }

            bool skipped = false;
            const HRESULT target_res =
                prepare_output_target(index, output_item_path, resolved_path, false, target, skipped);
            if (target_res != S_OK || skipped) {
                if (skipped) {
                    remember_skipped_archive_item(index);
                }
                return target_res;
            }

            if (!try_reserve_budget_file()) {
                return E_ABORT;
            }

            FilesystemObjectIdentity materialized_identity;
            if (std::optional<std::string> const warning =
                    materialize_link(target, link_plan, true, &materialized_identity);
                warning.has_value()) {
                release_budget_file();
                record_partial_warning(*warning);
            } else {
                std::error_code link_status_ec;
                fs::file_status const link_status = fs::symlink_status(target.output_path, link_status_ec);
                if (!link_status_ec
                    && fs::status_known(link_status)
                    && link_status.type() != fs::file_type::not_found) {
                    PendingLink pending;
                    pending.output_target = target;
                    pending.attributes = item_attributes;
                    pending.times = item_times;
                    pending.materialized_identity = materialized_identity;
                    if (!pending.materialized_identity.defined) {
                        release_budget_file();
                        record_io_error("Cannot identify created output link: " + target.output_path.generic_string());
                        return E_FAIL;
                    }
                    pending.is_symlink = link_plan.type == ExtractItemLinkInfo::Type::kSymLink;
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_link_ = std::move(pending);
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!deferred_hard_links_.empty()
                        && deferred_hard_links_.back().output_target.output_path == target.output_path) {
                        deferred_hard_links_.back().attributes = item_attributes;
                        deferred_hard_links_.back().times = item_times;
                        pending_deferred_hard_link_output_ = target.output_path;
                    }
                }
            }
            return S_OK;
        }

        uint64_t declared_size = 0;
        bool const declared_size_defined = archive_get_prop_uint64(archive_, index, kpidSize, declared_size);
        if (declared_size_defined
            && budget_tracker_ != nullptr
            && !budget_tracker_->can_accept_declared_bytes(declared_size)) {
            return E_ABORT;
        }

        OutputTarget target;
        bool skipped = false;
        const HRESULT target_res =
            prepare_output_target(index, output_item_path, resolved_path, false, target, skipped);
        if (target_res != S_OK || skipped) {
            if (skipped) {
                remember_skipped_archive_item(index);
            }
            return target_res;
        }

        if (!try_reserve_budget_file()) {
            return E_ABORT;
        }

        auto* stream = new NativeFileOutStream(target.temp_path, budget_tracker_);
        const HRESULT open_res = stream->open();
        if (open_res != S_OK) {
            std::string const failure = stream->failure_message();
            record_io_error(failure.empty()
                                ? "Cannot create temporary output file: " + target.temp_path.generic_string()
                                : failure);
            stream->Release();
            release_budget_file();
            return open_res;
        }

        std::error_code temp_identity_ec;
        FilesystemObjectIdentity const temp_identity =
            capture_filesystem_object_identity_no_follow(target.temp_path, temp_identity_ec);
        if (temp_identity_ec || !temp_identity.defined) {
            (void)stream->Close();
            stream->Release();
            release_budget_file();
            record_io_error("Cannot identify temporary extraction output: "
                            + target.temp_path.generic_string()
                            + (temp_identity_ec ? std::string("; ") + temp_identity_ec.message() : ""));
            return E_FAIL;
        }

        stream->AddRef();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            PendingEntry pe;
            pe.archive_entry_path = target.archive_entry_path;
            pe.archive_index = index;
            pe.absolute_output_path = target.absolute_output_path;
            pe.output_path = target.output_path;
            pe.destination_path = target.destination_path;
            pe.authorized_root = target.authorized_root;
            pe.temp_path = target.temp_path;
            pe.backup_path = target.backup_path;
            pe.transaction = target.transaction;
            pe.collided_archive_entry_path = target.collided_archive_entry_path;
            pe.original_identity = target.original_identity;
            pe.temp_identity = temp_identity;
            pe.had_original = target.had_original;
            pe.overwrote_existing = target.overwrote_existing;
            pe.renamed_from_collision = target.renamed_from_collision;
            pe.preserve_backup_on_commit = target.preserve_backup_on_commit;
            pe.budget_file_reserved = true;
            pe.attributes = item_attributes;
            pe.times = item_times;
            pe.owned_stream = stream;
            pending_entry_ = std::move(pe);
        }

        *out_stream = stream;
        return S_OK;
    }

} // namespace z7::app
