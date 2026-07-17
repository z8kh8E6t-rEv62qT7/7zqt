// src/archive_application/src/native_7z/operations/operations_extract.cpp
// Role: Extract operation pipeline and multi-archive dispatch.

#include "core/internal.h"
#include "operations/extract_output.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_extract.h"
#include "third_party_adapter/callbacks_extract_stream.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    CompressionResourcesEstimate estimate_compression_resources_for_request(AddRequest const& request);

    namespace {

        bool remap_source_matches_path(ExtractPathRemapMatchKind match_kind,
                                       std::string const& source_path,
                                       std::vector<std::string> const& selected_entries,
                                       std::string const& candidate_path) {
            auto const has_prefix = [](std::string const& prefix, std::string const& path) {
                if (prefix.empty()) {
                    return true;
                }
                if (path == prefix) {
                    return true;
                }
                return path.size() > prefix.size()
                    && path.compare(0, prefix.size(), prefix) == 0
                    && path[prefix.size()] == '/';
            };

            switch (match_kind) {
                case ExtractPathRemapMatchKind::kRequestRoot:
                    if (selected_entries.empty()) {
                        return true;
                    }
                    if (selected_entries.size() != 1) {
                        return false;
                    }
                    return has_prefix(selected_entries.front(), candidate_path);
                case ExtractPathRemapMatchKind::kExactArchivePath:
                    return candidate_path == source_path;
                case ExtractPathRemapMatchKind::kArchivePrefix:
                    return has_prefix(source_path, candidate_path);
            }
            return false;
        }

        std::string strip_source_prefix_for_remap(ExtractPathRemapMatchKind match_kind,
                                                  std::string const& source_path,
                                                  std::vector<std::string> const& selected_entries,
                                                  std::string const& candidate_path) {
            auto const strip_prefix = [](std::string const& prefix, std::string const& path) {
                if (prefix.empty()) {
                    return path;
                }
                if (path == prefix) {
                    return std::string{};
                }
                return path.substr(prefix.size() + 1);
            };

            switch (match_kind) {
                case ExtractPathRemapMatchKind::kRequestRoot:
                    if (selected_entries.empty()) {
                        return candidate_path;
                    }
                    if (selected_entries.size() != 1) {
                        return candidate_path;
                    }
                    return strip_prefix(selected_entries.front(), candidate_path);
                case ExtractPathRemapMatchKind::kExactArchivePath:
                    return {};
                case ExtractPathRemapMatchKind::kArchivePrefix:
                    return strip_prefix(source_path, candidate_path);
            }
            return candidate_path;
        }

        std::vector<std::string> normalized_selected_entries_for_request(ExtractRequest const& request) {
            std::vector<std::string> normalized;
            normalized.reserve(request.entries.size());
            for (std::string const& entry : request.entries) {
                std::string const norm = normalize_archive_item_path(entry);
                if (!norm.empty()) {
                    normalized.push_back(norm);
                }
            }
            return normalized;
        }

        HRESULT alternate_stream_parent_matches_selection(
            CArc const* arc,
            UInt32 index,
            std::unordered_set<std::string> const& selected_entries,
            bool& matches) {
            matches = false;
            IInArchive* archive = arc != nullptr ? arc->Archive : nullptr;
            bool is_alternate_stream = false;
            if (archive == nullptr
                || !archive_get_prop_bool(archive, index, kpidIsAltStream, is_alternate_stream)
                || !is_alternate_stream) {
                return S_OK;
            }
            CMyComPtr<IArchiveGetRawProps> raw_props;
            if (archive->QueryInterface(IID_IArchiveGetRawProps, reinterpret_cast<void**>(&raw_props)) != S_OK
                || !raw_props) {
                return S_OK;
            }
            UInt32 parent_index = static_cast<UInt32>(-1);
            UInt32 parent_type = NParentType::kDir;
            if (raw_props->GetParent(index, &parent_index, &parent_type) != S_OK
                || parent_type != NParentType::kAltStream
                || parent_index == static_cast<UInt32>(-1)) {
                return S_OK;
            }
            ArchiveItemPath parent_path;
            HRESULT const path_result = resolve_archive_item_path(arc, parent_index, parent_path);
            if (path_result != S_OK) {
                return path_result;
            }
            matches = archive_path_matches_selection(parent_path.normalized, selected_entries);
            return S_OK;
        }

        std::optional<std::pair<std::string, bool>>
        primary_output_from_remaps(ExtractRequest const& request,
                                   std::vector<ExtractMaterializedEntry> const& entries) {
            if (entries.empty() || request.path_remaps.empty()) {
                return std::nullopt;
            }

            std::vector<std::string> const selected_entries = normalized_selected_entries_for_request(request);

            auto const specificity = [](ExtractPathRemap const& remap) {
                switch (remap.match_kind) {
                    case ExtractPathRemapMatchKind::kExactArchivePath:
                        return static_cast<int>(remap.source_path.size()) * 2 + 2;
                    case ExtractPathRemapMatchKind::kArchivePrefix:
                        return static_cast<int>(remap.source_path.size()) * 2 + 1;
                    case ExtractPathRemapMatchKind::kRequestRoot:
                        return 0;
                }
                return -1;
            };

            for (auto const& entry : entries) {
                ExtractPathRemap const* best = nullptr;
                std::string best_source;
                int best_specificity = -1;
                bool ambiguous = false;
                for (ExtractPathRemap const& remap : request.path_remaps) {
                    std::string const source_path = normalize_archive_item_path(remap.source_path);
                    if (remap.match_kind == ExtractPathRemapMatchKind::kRequestRoot
                        && !(selected_entries.empty() || selected_entries.size() == 1)) {
                        continue;
                    }
                    if (!remap_source_matches_path(
                            remap.match_kind, source_path, selected_entries, entry.archive_entry_path)) {
                        continue;
                    }
                    int const score = specificity(remap);
                    if (score > best_specificity) {
                        best = &remap;
                        best_source = source_path;
                        best_specificity = score;
                        ambiguous = false;
                    } else if (score == best_specificity) {
                        ambiguous = true;
                    }
                }
                if (best == nullptr || ambiguous) {
                    continue;
                }

                std::error_code canonical_ec;
                fs::path remap_destination_path = fs::weakly_canonical(fs::path(best->destination_path), canonical_ec);
                if (canonical_ec) {
                    canonical_ec.clear();
                    remap_destination_path = fs::absolute(fs::path(best->destination_path), canonical_ec);
                }
                std::string const remap_destination = remap_destination_path.generic_string();
                std::string const relative_tail = strip_source_prefix_for_remap(
                    best->match_kind, best_source, selected_entries, entry.archive_entry_path);
                if (relative_tail.empty()) {
                    return std::make_pair(remap_destination, entry.is_directory);
                }
                if (entry.absolute_output_path == remap_destination) {
                    return std::make_pair(remap_destination, entry.is_directory);
                }
                return std::make_pair(remap_destination, true);
            }

            return std::nullopt;
        }

        struct RollbackAttemptResult {
            bool ok = true;
            std::string first_error;
        };

        std::optional<std::string> cleanup_extract_overwrite_backups(std::vector<ExtractRollbackEntry> const& entries) {
            for (ExtractRollbackEntry const& entry : entries) {
                if (!entry.had_original || entry.preserve_backup_on_commit || entry.backup_path.empty()) {
                    continue;
                }
                if (entry.transaction == nullptr || !entry.backup_identity.defined) {
                    return "Overwrite backup has no owning filesystem transaction: "
                         + entry.backup_path.generic_string();
                }
                TransactionMoveResult const discarded =
                    entry.transaction->discard(entry.backup_path, &entry.backup_identity);
                if (!discarded.success) {
                    return "Overwrite backup changed before cleanup: "
                         + entry.backup_path.generic_string()
                         + "; "
                         + discarded.diagnostic;
                }
                std::string finish_diagnostic;
                (void)entry.transaction->finish(&finish_diagnostic);
            }
            return std::nullopt;
        }

        void attach_extract_cleanup_error(ExtractResult& result, std::optional<std::string> const& cleanup_error) {
            if (!cleanup_error.has_value()) {
                return;
            }
            result.ok = false;
            result.error = make_archive_error(ArchiveErrorDomain::kIo, *cleanup_error, 2);
            result.native_exit_code = 2;
            result.native_execution.native_exit_code = 2;
            result.summary = *cleanup_error;
        }

        RollbackAttemptResult rollback_extract_entries(std::vector<ExtractRollbackEntry> const& entries) {
            RollbackAttemptResult result;

            auto fail = [&](std::string message) {
                result.ok = false;
                if (result.first_error.empty()) {
                    result.first_error = std::move(message);
                }
            };

            struct DirectoryMetadataRestore {
                ExtractRollbackEntry const* entry = nullptr;
            };
            std::vector<DirectoryMetadataRestore> metadata_restores;

            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                ExtractRollbackEntry const& entry = *it;
                std::error_code ec;

                if (entry.restore_directory_metadata) {
                    if (entry.transaction != nullptr) {
                        std::string finish_diagnostic;
                        if (!entry.transaction->finish(&finish_diagnostic)) {
                            fail("rollback incomplete: " + finish_diagnostic);
                        }
                    }
                    metadata_restores.push_back(DirectoryMetadataRestore{&entry});
                    continue;
                }

                fs::file_status const output_status = fs::symlink_status(entry.output_path, ec);
                bool output_missing = false;
                if (ec == std::errc::no_such_file_or_directory) {
                    output_missing = true;
                    ec.clear();
                } else if (ec) {
                    fail("Failed to inspect rollback output: " + ec.message());
                    continue;
                } else {
                    output_missing =
                        !fs::status_known(output_status) || output_status.type() == fs::file_type::not_found;
                }

                if (!output_missing
                    && !filesystem_object_matches_identity_no_follow(entry.output_path, entry.output_identity, ec)) {
                    fail("Rollback output was replaced by another filesystem object: "
                         + entry.output_path.generic_string());
                    continue;
                }

                std::shared_ptr<FilesystemTransaction> transaction = entry.transaction;
                if (transaction == nullptr) {
                    std::unique_ptr<FilesystemTransaction> created =
                        FilesystemTransaction::create(entry.output_path, "extract-rollback", ec);
                    if (!created) {
                        fail("Failed to create rollback transaction: " + ec.message());
                        continue;
                    }
                    transaction = std::shared_ptr<FilesystemTransaction>(std::move(created));
                }

                fs::path quarantined_output;
                if (!output_missing) {
                    TransactionMoveResult const quarantined =
                        transaction->quarantine(entry.output_path, &entry.output_identity);
                    if (!quarantined.success) {
                        fail("rollback incomplete: " + quarantined.diagnostic);
                        continue;
                    }
                    quarantined_output = quarantined.preserved_path;
                }

                if (entry.had_original) {
                    if (entry.backup_path.empty() || !entry.backup_identity.defined) {
                        fail("Cannot restore overwritten output without a backup: "
                             + entry.output_path.generic_string());
                        continue;
                    }
                    fs::path owned_backup = entry.backup_path;
                    if (owned_backup.parent_path().lexically_normal() != transaction->directory().lexically_normal()) {
                        TransactionMoveResult const quarantined_backup =
                            transaction->quarantine(entry.backup_path, &entry.backup_identity);
                        if (!quarantined_backup.success) {
                            fail("Overwrite backup was replaced or removed: " + quarantined_backup.diagnostic);
                            continue;
                        }
                        owned_backup = quarantined_backup.preserved_path;
                    } else if (!filesystem_object_matches_identity_no_follow(
                                   owned_backup, entry.backup_identity, ec)) {
                        fail("Overwrite backup was replaced or removed: " + entry.backup_path.generic_string());
                        continue;
                    }
                    TransactionMoveResult const restored = transaction->restore(owned_backup, entry.destination_path);
                    if (!restored.success) {
                        fail("rollback incomplete: " + restored.diagnostic);
                        continue;
                    }
                }

                if (!quarantined_output.empty()) {
                    TransactionMoveResult const discarded =
                        transaction->discard(quarantined_output, &entry.output_identity);
                    if (!discarded.success) {
                        fail("rollback incomplete: " + discarded.diagnostic);
                    }
                }
                std::string finish_diagnostic;
                if (!transaction->finish(&finish_diagnostic)) {
                    fail("rollback incomplete: " + finish_diagnostic);
                }
            }

            // Directory metadata is deliberately last: deleting children changes
            // parent mtimes, so restoring it during structural rollback is wrong.
            std::reverse(metadata_restores.begin(), metadata_restores.end());
            std::unordered_set<std::string> restored_directories;
            for (DirectoryMetadataRestore const& restore : metadata_restores) {
                ExtractRollbackEntry const& entry = *restore.entry;
                std::string const identity_key = std::to_string(entry.output_identity.volume) + ":"
                                               + std::to_string(entry.output_identity.object);
                if (!restored_directories.insert(identity_key).second) {
                    continue;
                }
                std::error_code ec;
                if (!filesystem_object_matches_identity_no_follow(entry.output_path, entry.output_identity, ec)) {
                    fail("Directory disappeared or changed before metadata rollback: "
                         + entry.output_path.generic_string());
                    continue;
                }
                fs::permissions(entry.output_path, entry.original_permissions, fs::perm_options::replace, ec);
                if (!ec && entry.original_mtime_defined) {
                    fs::last_write_time(entry.output_path, entry.original_mtime, ec);
                }
                if (ec) {
                    fail("Failed to restore directory metadata: " + ec.message());
                }
            }

            return result;
        }

        // Core extract logic given an already-open CArc. Shared by the standard
        // path-based flow and the session-token reuse flow.
        ExtractResult run_extract_on_arc(CArc const* arc,
                                         UInt32 num_items,
                                         ExtractRequest const& request,
                                         std::string const& archive_metadata_source_path,
                                         ArchiveBackendHooks const& hooks,
                                         std::atomic<bool>& cancel_requested,
                                         std::function<bool()> wait_while_paused,
                                         std::shared_ptr<ExtractBudgetTracker> const& budget_tracker,
                                         std::vector<ExtractRollbackEntry>& request_rollback_entries,
                                         OpenArchiveDiagnostics const* open_diagnostics,
                                         bool open_diagnostic_already_published) {
            ReadOperationOpenDiagnosticState const open_diagnostic_state =
                publish_read_operation_open_diagnostics(
                    hooks,
                    open_diagnostics,
                    open_diagnostic_already_published);
            std::unordered_set<std::string> selected_entries;
            selected_entries.reserve(request.entries.size());
            for (std::string const& entry : request.entries) {
                std::string const normalized = normalize_archive_item_path(entry);
                if (!normalized.empty()) {
                    selected_entries.insert(normalized);
                }
            }

            ExtractArchiveItemStats item_stats;
            std::vector<UInt32> selected_indices;
            selected_indices.reserve(static_cast<size_t>(num_items));

            if (selected_entries.empty()) {
                for (UInt32 i = 0; i < num_items; ++i) {
                    accumulate_extract_item_stats(arc->Archive, i, item_stats);
                }
            } else {
                for (UInt32 i = 0; i < num_items; ++i) {
                    ArchiveItemPath item_path;
                    HRESULT const path_result = resolve_archive_item_path(arc, i, item_path);
                    if (path_result != S_OK) {
                        return from_base_result<ExtractResult>(
                            make_operation_failure_from_hresult<OperationResult>(path_result));
                    }
                    if (!archive_path_matches_selection(item_path.normalized, selected_entries)) {
                        bool parent_matches = false;
                        HRESULT const parent_result =
                            alternate_stream_parent_matches_selection(arc, i, selected_entries, parent_matches);
                        if (parent_result != S_OK) {
                            return from_base_result<ExtractResult>(
                                make_operation_failure_from_hresult<OperationResult>(parent_result));
                        }
                        if (!parent_matches) {
                            continue;
                        }
                    }
                    selected_indices.push_back(i);
                    accumulate_extract_item_stats(arc->Archive, i, item_stats);
                }
            }

            emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kNone, "Archives: 1");
            if (arc->PhySize_Defined) {
                emit_log_event(hooks,
                               OperationStage::kRunning,
                               OutputChannel::kNone,
                               "Physical Size = " + std::to_string(arc->PhySize));
            }
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Folders: " + std::to_string(item_stats.num_dirs));
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Files: " + std::to_string(item_stats.num_files));
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Size = " + std::to_string(item_stats.total_unpacked_size));

            uint64_t const total_files = selected_entries.empty() ? static_cast<uint64_t>(num_items)
                                                                  : static_cast<uint64_t>(selected_indices.size());
            emit_progress_event(hooks,
                                OperationStage::kRunning,
                                -1,
                                arc->PhySize_Defined,
                                arc->PhySize_Defined ? arc->PhySize : 0,
                                0,
                                total_files,
                                0,
                                open_diagnostic_state.progress_error_count,
                                {},
                                {});

            if (!selected_entries.empty() && selected_indices.empty()) {
                return make_operation_success<ExtractResult>("Everything is Ok");
            }

            std::string eliminate_prefix;
            if (request.eliminate_root_duplication && request.path_mode != ExtractPathMode::kAbsolutePaths) {
                std::string const candidate = normalize_archive_item_path(output_tail_name(request.output_dir));
                if (!candidate.empty()) {
                    bool possible = true;
                    auto check_index = [&](UInt32 i) {
                        if (!possible) {
                            return;
                        }
                        std::string const item_path =
                            normalize_archive_item_path(archive_get_prop_text(arc->Archive, i, kpidPath));
                        if (item_path.empty()) {
                            possible = false;
                            return;
                        }
                        if (item_path == candidate) {
                            bool item_is_dir = false;
                            (void)archive_get_prop_bool(arc->Archive, i, kpidIsDir, item_is_dir);
                            if (!item_is_dir) {
                                possible = false;
                            }
                            return;
                        }
                        if (item_path.size() <= candidate.size()
                            || item_path.compare(0, candidate.size(), candidate) != 0
                            || item_path[candidate.size()] != '/') {
                            possible = false;
                        }
                    };

                    if (selected_entries.empty()) {
                        for (UInt32 i = 0; i < num_items; ++i) {
                            check_index(i);
                            if (!possible) {
                                break;
                            }
                        }
                    } else {
                        for (UInt32 i : selected_indices) {
                            check_index(i);
                            if (!possible) {
                                break;
                            }
                        }
                    }

                    if (possible) {
                        eliminate_prefix = candidate;
                    }
                }
            }

            auto* callback =
                new NativeExtractCallback(arc,
                                          fs::path(request.output_dir),
                                          hooks,
                                          &cancel_requested,
                                          wait_while_paused,
                                          request.archive_path,
                                          std::vector<std::string>(selected_entries.begin(), selected_entries.end()),
                                          request.overwrite_mode,
                                          request.path_mode,
                                          eliminate_prefix,
                                          request.path_remaps,
                                          request.password,
                                          request.zone_id_mode,
                                          request.restore_file_security,
                                          total_files,
                                          request.budget,
                                          budget_tracker,
                                          request.configured_memory_limit_bytes,
                                          request.configured_memory_limit_defined,
                                          archive_metadata_source_path,
                                          open_diagnostic_state.progress_error_count,
                                          open_diagnostic_state.archive_context_reported);

            UInt32 const* indices = nullptr;
            UInt32 num_indices = static_cast<UInt32>(-1);
            if (!selected_entries.empty()) {
                indices = selected_indices.data();
                num_indices = static_cast<UInt32>(selected_indices.size());
            }

            ExtractInvocationStatus status =
                invoke_archive_extract_with_callback(arc->Archive, indices, num_indices, false, callback);

            std::string const truncate_summary = "Extract truncated: budget exceeded; partial results kept.";
            if (status.budget_triggered && status.budget_policy == BudgetExceededAction::kTruncate) {
                status.hresult = S_OK;
                ++status.error_count;
                if (!status.diagnostic.empty()) {
                    status.diagnostic.push_back('\n');
                }
                status.diagnostic += truncate_summary;
                emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kStdErr, truncate_summary);
            }

            request_rollback_entries.reserve(request_rollback_entries.size() + status.rollback_entries.size());
            for (ExtractRollbackEntry& entry : status.rollback_entries) {
                request_rollback_entries.push_back(std::move(entry));
            }

            ExtractResult result = finalize_extract_operation_result<ExtractResult>(
                hooks,
                cancel_requested,
                total_files,
                status,
                [](ExtractInvocationStatus const& done) {
                    if (!done.diagnostic.empty()) {
                        return make_operation_partial_success<ExtractResult>(done.diagnostic);
                    }
                    return make_operation_partial_success<ExtractResult>();
                },
                [](ExtractInvocationStatus const&) {
                    return make_operation_success<ExtractResult>("Everything is Ok");
                },
                "Password required or incorrect",
                open_diagnostics);

            // Budget exceeded: replace the callback HRESULT result with a coherent
            // operation outcome. The extraction callback uses E_ABORT only to stop
            // materialization at the configured boundary; that is not user cancel.
            if (status.budget_triggered) {
                std::vector<ExtractMaterializedEntry> materialized_entries = std::move(status.materialized_entries);
                switch (status.budget_policy) {
                    case BudgetExceededAction::kFailAndRollback:
                        {
                            RollbackAttemptResult const rollback = rollback_extract_entries(request_rollback_entries);
                            std::string summary;
                            if (rollback.ok) {
                                materialized_entries.clear();
                                summary = "Extract stopped: budget exceeded; extracted files removed.";
                            } else {
                                summary = rollback.first_error.empty()
                                              ? "Extract stopped: budget exceeded; rollback incomplete and "
                                                "destination files may have been modified."
                                              : "Extract stopped: budget exceeded; rollback incomplete: "
                                                    + rollback.first_error;
                            }
                            result = make_operation_failure<ExtractResult>(
                                ArchiveErrorDomain::kBudgetExceeded, std::move(summary), 2);
                            break;
                        }
                    case BudgetExceededAction::kFailAndKeepPartial:
                        result = make_operation_failure<ExtractResult>(
                            ArchiveErrorDomain::kBudgetExceeded,
                            "Extract stopped: budget exceeded; partial results kept.",
                            2);
                        break;
                    case BudgetExceededAction::kTruncate:
                        result = make_operation_warning_success<ExtractResult>(truncate_summary);
                        break;
                }
                result.materialized_entries = std::move(materialized_entries);
                result.primary_output_path.clear();
                result.primary_is_directory = false;
                if (open_diagnostics != nullptr) {
                    apply_open_archive_diagnostics(result, *open_diagnostics);
                }
                return result;
            }

            // Backfill materialized entries from callback (available even on cancel/error
            // so callers can clean up partially-written files).
            result.materialized_entries = std::move(status.materialized_entries);

            if (auto const remapped_primary = primary_output_from_remaps(request, result.materialized_entries);
                remapped_primary.has_value()) {
                result.primary_output_path = remapped_primary->first;
                result.primary_is_directory = remapped_primary->second;
                return result;
            }

            // Compute primary_output_path for single-logical-entry requests.
            if (request.entries.size() == 1 && !result.materialized_entries.empty()) {
                std::string const norm_entry = normalize_archive_item_path(request.entries[0]);
                // First try: exact match on archive_entry_path.
                for (auto const& e : result.materialized_entries) {
                    if (e.archive_entry_path == norm_entry) {
                        result.primary_output_path = e.absolute_output_path;
                        result.primary_is_directory = e.is_directory;
                        break;
                    }
                }
                // Second try: derived candidate path via output_dir + norm_entry.
                if (result.primary_output_path.empty()) {
                    std::error_code ec;
                    std::string const candidate =
                        fs::absolute(fs::path(request.output_dir) / norm_entry, ec).generic_string();
                    if (!ec) {
                        for (auto const& e : result.materialized_entries) {
                            if (e.absolute_output_path == candidate) {
                                result.primary_output_path = candidate;
                                result.primary_is_directory = e.is_directory;
                                break;
                            }
                        }
                        // Third try: subtree — any child exists under norm_entry prefix.
                        if (result.primary_output_path.empty()) {
                            std::string const prefix = norm_entry + '/';
                            for (auto const& e : result.materialized_entries) {
                                if (e.archive_entry_path.size() > prefix.size()
                                    && e.archive_entry_path.compare(0, prefix.size(), prefix) == 0) {
                                    result.primary_output_path = candidate;
                                    result.primary_is_directory = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            return result;
        }

    } // namespace

    ExtractResult NativeArchiveBackend::extract(ExtractRequest const& request, ArchiveBackendHooks const& hooks) {
        if (request.session_token.has_value() && request.session_token->is_valid() && !request.archive_paths.empty()) {
            return make_operation_failure<ExtractResult>(
                ArchiveErrorDomain::kInvalidArguments,
                "ExtractRequest cannot combine session_token with archive_paths",
                7);
        }

        auto const budget_tracker = std::make_shared<ExtractBudgetTracker>(request.budget);
        std::vector<ExtractRollbackEntry> request_rollback_entries;
        std::function<ExtractResult(ExtractRequest const&, ArchiveBackendHooks const&)> extract_single;
        extract_single = [&](ExtractRequest const& single_request,
                             ArchiveBackendHooks const& single_hooks) -> ExtractResult {
            // Token path: reuse an already-opened archive.
            if (single_request.session_token.has_value() && single_request.session_token->is_valid()) {
                auto session = ArchiveSessionRegistry::instance().find(*single_request.session_token);
                if (!session) {
                    return make_operation_failure<ExtractResult>(
                        ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
                }
                std::unique_lock<std::recursive_mutex> session_lock(
                    ArchiveOpenSessionNativeAccess::operation_mutex(*session));
                ScopedFilenameCodePage filename_scope(session->filename_code_page());
                if (ArchiveOpenSessionNativeAccess::closed(*session)) {
                    return make_operation_failure<ExtractResult>(
                        ArchiveErrorDomain::kInvalidArguments, "Archive session is already closed", 7);
                }
                CArc const* arc = archive_session_link(*session).GetArc();
                if (arc == nullptr || arc->Archive == nullptr) {
                    return make_operation_failure<ExtractResult>(
                        ArchiveErrorDomain::kInvalidArguments, "Session archive unavailable", 7);
                }
                UInt32 num_items = 0;
                if (arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
                    return make_operation_failure<ExtractResult>(
                        ArchiveErrorDomain::kUnknown, "GetNumberOfItems failed", 2);
                }
                ExtractRequest session_request = single_request;
                // In token mode every archive identity comes from the session.
                // Caller-provided archive_path is untrusted display input and is
                // intentionally ignored.
                session_request.archive_path = session->display_path();
                std::shared_ptr<ArchiveOpenSession> metadata_root = session;
                while (ArchiveOpenSessionNativeAccess::parent(*metadata_root) != nullptr) {
                    metadata_root = ArchiveOpenSessionNativeAccess::parent(*metadata_root);
                }
                std::string const metadata_source =
                    ArchiveOpenSessionNativeAccess::source_archive_path(*metadata_root);
                if (!session_request.password.empty()) {
                    session->set_password(session_request.password);
                } else if (session->password_defined()) {
                    session_request.password = session->password();
                }

                ArchiveBackendHooks const session_hooks = make_session_password_hooks(*session, single_hooks);
                ExtractResult result = run_extract_on_arc(
                    arc,
                    num_items,
                    session_request,
                    metadata_source,
                    session_hooks,
                    cancel_requested_,
                    [this]() { return this->wait_while_paused(); },
                    budget_tracker,
                    request_rollback_entries,
                    &archive_session_state(*session).open_diagnostics,
                    false);
                if (!result.ok && result.error.domain == ArchiveErrorDomain::kPassword) {
                    session->clear_password();
                }
                return result;
            }

            return run_open_archive_read_pipeline<ExtractResult>(
                single_request.archive_path,
                single_request.archive_type_hint,
                single_hooks,
                OpenResultMessagePolicy::kReadOperationMessages,
                true,
                single_request.password,
                [&](OpenArchiveReadState const& open_state, UInt32 num_items) -> ExtractResult {
                    return run_extract_on_arc(
                        open_state.arc,
                        num_items,
                        single_request,
                        single_request.archive_path,
                        single_hooks,
                        cancel_requested_,
                        [this]() { return this->wait_while_paused(); },
                        budget_tracker,
                        request_rollback_entries,
                        &open_state.open_diagnostics,
                        true);
                });
        };

        if (!request.archive_paths.empty()) {
            ExtractResult merged;
            bool has_any = false;
            size_t nonempty_archive_count = 0;
            for (std::string const& archive : request.archive_paths) {
                if (!archive.empty()) {
                    ++nonempty_archive_count;
                }
            }
            for (std::string const& archive : request.archive_paths) {
                if (archive.empty()) {
                    continue;
                }
                ExtractRequest single = request;
                single.archive_path = archive;
                single.archive_paths.clear();
                single.output_dir = resolve_multi_archive_output_dir(request.output_dir, archive);
                ExtractResult step = extract_single(single, hooks);
                bool const rolled_back = step.error.domain == ArchiveErrorDomain::kBudgetExceeded
                                      && request.budget.has_value()
                                      && request.budget->on_exceeded == BudgetExceededAction::kFailAndRollback;
                std::vector<ExtractMaterializedEntry> accumulated =
                    rolled_back ? std::vector<ExtractMaterializedEntry>{} : std::move(merged.materialized_entries);
                accumulated.reserve(accumulated.size() + step.materialized_entries.size());
                for (auto& e : step.materialized_entries) {
                    accumulated.push_back(std::move(e));
                }
                step.materialized_entries = std::move(accumulated);
                if (nonempty_archive_count > 1) {
                    step.primary_output_path.clear();
                    step.primary_is_directory = false;
                }
                if (!step.ok || step.error.domain == ArchiveErrorDomain::kBudgetExceeded) {
                    if (!rolled_back) {
                        attach_extract_cleanup_error(step, cleanup_extract_overwrite_backups(request_rollback_entries));
                    }
                    return step;
                }
                merged = std::move(step);
                has_any = true;
            }
            if (!has_any) {
                return merged;
            }
            attach_extract_cleanup_error(merged, cleanup_extract_overwrite_backups(request_rollback_entries));
            return merged;
        }
        ExtractResult result = extract_single(request, hooks);
        bool const rolled_back = result.error.domain == ArchiveErrorDomain::kBudgetExceeded
                              && request.budget.has_value()
                              && request.budget->on_exceeded == BudgetExceededAction::kFailAndRollback;
        if (!rolled_back) {
            attach_extract_cleanup_error(result, cleanup_extract_overwrite_backups(request_rollback_entries));
        }
        return result;
    }

    CompressionResourcesEstimate estimate_compression_resources_native(AddRequest const& request) {
        return estimate_compression_resources_for_request(request);
    }

} // namespace z7::app
