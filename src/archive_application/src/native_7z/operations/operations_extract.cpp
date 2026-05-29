// src/archive_application/src/native_7z/operations/operations_extract.cpp
// Role: Extract operation pipeline and multi-archive dispatch.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract.h"
#include "operations/extract_output.h"
#include "session/session_registry_internal.h"

namespace z7::app {

CompressionResourcesEstimate estimate_compression_resources_for_request(
    const AddRequest& request);

namespace {

bool remap_source_matches_path(ExtractPathRemapMatchKind match_kind,
                               const std::string& source_path,
                               const std::vector<std::string>& selected_entries,
                               const std::string& candidate_path) {
  const auto has_prefix = [](const std::string& prefix,
                             const std::string& path) {
    if (prefix.empty()) {
      return true;
    }
    if (path == prefix) {
      return true;
    }
    return path.size() > prefix.size() &&
           path.compare(0, prefix.size(), prefix) == 0 &&
           path[prefix.size()] == '/';
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
                                          const std::string& source_path,
                                          const std::vector<std::string>& selected_entries,
                                          const std::string& candidate_path) {
  const auto strip_prefix = [](const std::string& prefix,
                               const std::string& path) {
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

ArchiveBackendHooks hooks_with_extract_open_password(
    const ExtractRequest& request,
    const ArchiveBackendHooks& hooks) {
  if (request.password.empty()) {
    return hooks;
  }

  ArchiveBackendHooks wrapped = hooks;
  wrapped.ask_password =
      [password = request.password,
       provided = false,
       base = hooks.ask_password](const PasswordPrompt& prompt) mutable {
        if (!provided &&
            prompt.reason_kind != PasswordPromptReason::kWrongPassword) {
          provided = true;
          PasswordReply reply;
          reply.kind = PasswordReplyKind::kProvide;
          reply.password = password;
          return reply;
        }
        if (base) {
          return base(prompt);
        }
        return PasswordReply{};
      };
  return wrapped;
}

bool extract_open_requires_callback(const ExtractRequest& request,
                                    const ArchiveBackendHooks& hooks) {
  return !request.password.empty() || static_cast<bool>(hooks.ask_password);
}

std::vector<std::string> normalized_selected_entries_for_request(
    const ExtractRequest& request) {
  std::vector<std::string> normalized;
  normalized.reserve(request.entries.size());
  for (const std::string& entry : request.entries) {
    const std::string norm = normalize_archive_item_path(entry);
    if (!norm.empty()) {
      normalized.push_back(norm);
    }
  }
  return normalized;
}

std::optional<std::pair<std::string, bool>> primary_output_from_remaps(
    const ExtractRequest& request,
    const std::vector<ExtractMaterializedEntry>& entries) {
  if (entries.empty() || request.path_remaps.empty()) {
    return std::nullopt;
  }

  const std::vector<std::string> selected_entries =
      normalized_selected_entries_for_request(request);

  for (const ExtractPathRemap& remap : request.path_remaps) {
    const std::string source_path = normalize_archive_item_path(remap.source_path);
    if (remap.match_kind == ExtractPathRemapMatchKind::kRequestRoot &&
        !(selected_entries.empty() || selected_entries.size() == 1)) {
      continue;
    }

    const std::string remap_destination =
        fs::absolute(fs::path(remap.destination_path)).generic_string();
    for (const auto& entry : entries) {
      if (!remap_source_matches_path(remap.match_kind,
                                     source_path,
                                     selected_entries,
                                     entry.archive_entry_path)) {
        continue;
      }
      const std::string relative_tail = strip_source_prefix_for_remap(
          remap.match_kind, source_path, selected_entries, entry.archive_entry_path);
      if (relative_tail.empty()) {
        return std::make_pair(remap_destination, entry.is_directory);
      }
      if (entry.absolute_output_path == remap_destination) {
        return std::make_pair(remap_destination, entry.is_directory);
      }
      return std::make_pair(remap_destination, true);
    }
  }

  return std::nullopt;
}

struct RollbackAttemptResult {
  bool ok = true;
  std::string first_error;
};

void cleanup_extract_overwrite_backups_best_effort(
    const std::vector<ExtractRollbackEntry>& entries) {
  for (const ExtractRollbackEntry& entry : entries) {
    if (!entry.had_original || entry.preserve_backup_on_commit ||
        entry.backup_path.empty()) {
      continue;
    }
    std::error_code ec;
    remove_path_any(entry.backup_path, ec);
  }
}

RollbackAttemptResult rollback_extract_entries(
    const std::vector<ExtractRollbackEntry>& entries) {
  RollbackAttemptResult result;

  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const ExtractRollbackEntry& entry = *it;
    std::error_code ec;

    if (entry.had_original) {
      remove_path_any(entry.output_path, ec);
      ec.clear();
      fs::rename(entry.backup_path, entry.destination_path, ec);
      if (ec && result.first_error.empty()) {
        result.ok = false;
        result.first_error =
            "Failed to restore overwritten output: " + ec.message();
      }
      continue;
    }

    fs::remove_all(entry.output_path, ec);
    if (ec && result.first_error.empty()) {
      result.ok = false;
      result.first_error =
          "Failed to remove rollback output: " + ec.message();
    }
  }

  return result;
}

// Core extract logic given an already-open CArc. Shared by the standard
// path-based flow and the session-token reuse flow.
ExtractResult run_extract_on_arc(const CArc* arc,
                                 UInt32 num_items,
                                 const ExtractRequest& request,
                                 const ArchiveBackendHooks& hooks,
                                 std::atomic<bool>& cancel_requested,
                                 std::function<bool()> wait_while_paused) {
  std::unordered_set<std::string> selected_entries;
  selected_entries.reserve(request.entries.size());
  for (const std::string& entry : request.entries) {
    const std::string normalized = normalize_archive_item_path(entry);
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
      const std::string item_path =
          archive_item_selection_path(arc->Archive, i);
      if (!archive_path_matches_selection(item_path, selected_entries)) {
        continue;
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

  const uint64_t total_files = selected_entries.empty()
                                   ? static_cast<uint64_t>(num_items)
                                   : static_cast<uint64_t>(selected_indices.size());
  emit_progress_event(hooks,
                      OperationStage::kRunning,
                      -1,
                      arc->PhySize_Defined,
                      arc->PhySize_Defined ? arc->PhySize : 0,
                      0,
                      total_files,
                      0,
                      0,
                      {},
                      {});

  if (!selected_entries.empty() && selected_indices.empty()) {
    return make_operation_success<ExtractResult>("Everything is Ok");
  }

  std::string eliminate_prefix;
  if (request.eliminate_root_duplication &&
      request.path_mode != ExtractPathMode::kAbsolutePaths) {
    const std::string candidate =
        normalize_archive_item_path(output_tail_name(request.output_dir));
    if (!candidate.empty()) {
      bool possible = true;
      auto check_index = [&](UInt32 i) {
        if (!possible) {
          return;
        }
        const std::string item_path =
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
        if (item_path.size() <= candidate.size() ||
            item_path.compare(0, candidate.size(), candidate) != 0 ||
            item_path[candidate.size()] != '/') {
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

  auto* callback = new NativeExtractCallback(arc->Archive,
                                              fs::path(request.output_dir),
                                              hooks,
                                              &cancel_requested,
                                              wait_while_paused,
                                              request.archive_path,
                                              std::vector<std::string>(selected_entries.begin(),
                                                                       selected_entries.end()),
                                              request.overwrite_mode,
                                              request.path_mode,
                                              eliminate_prefix,
                                              request.path_remaps,
                                              request.password,
                                              request.zone_id_mode,
                                              request.restore_file_security,
                                              total_files,
                                              request.budget,
                                              request.configured_memory_limit_bytes,
                                              request.configured_memory_limit_defined);

  const UInt32* indices = nullptr;
  UInt32 num_indices = static_cast<UInt32>(-1);
  if (!selected_entries.empty()) {
    indices = selected_indices.data();
    num_indices = static_cast<UInt32>(selected_indices.size());
  }

  ExtractInvocationStatus status = invoke_archive_extract_with_callback(
      arc->Archive, indices, num_indices, false, callback);

  ExtractResult result = finalize_extract_operation_result<ExtractResult>(
      hooks,
      cancel_requested,
      total_files,
      status,
      [](const ExtractInvocationStatus& done) {
        if (!done.diagnostic.empty()) {
          return make_operation_partial_success<ExtractResult>(done.diagnostic);
        }
        return make_operation_partial_success<ExtractResult>();
      },
      [](const ExtractInvocationStatus&) {
        return make_operation_success<ExtractResult>("Everything is Ok");
      });

  // Budget exceeded: override result with the appropriate outcome.
  if (status.budget_triggered) {
    result.materialized_entries = std::move(status.materialized_entries);
    result.error.domain = ArchiveErrorDomain::kBudgetExceeded;
    result.error.message = status.budget_trigger_reason;
    switch (status.budget_policy) {
      case BudgetExceededAction::kFailAndRollback: {
        const RollbackAttemptResult rollback =
            rollback_extract_entries(status.rollback_entries);
        result.ok = false;
        if (rollback.ok) {
          result.materialized_entries.clear();
          result.summary =
              "Extract stopped: budget exceeded; extracted files removed.";
        } else {
          result.summary =
              rollback.first_error.empty()
                  ? "Extract stopped: budget exceeded; rollback incomplete and "
                    "destination files may have been modified."
                  : "Extract stopped: budget exceeded; rollback incomplete: " +
                        rollback.first_error;
        }
        break;
      }
      case BudgetExceededAction::kFailAndKeepPartial:
        cleanup_extract_overwrite_backups_best_effort(status.rollback_entries);
        result.ok = false;
        result.summary = "Extract stopped: budget exceeded; partial results kept.";
        break;
      case BudgetExceededAction::kTruncate:
        cleanup_extract_overwrite_backups_best_effort(status.rollback_entries);
        result.ok = true;
        result.error.domain = ArchiveErrorDomain::kPartialSuccess;
        result.summary = "Extract truncated: budget exceeded; partial results kept.";
        break;
    }
    result.primary_output_path.clear();
    result.primary_is_directory = false;
    return result;
  }

  // Backfill materialized entries from callback (available even on cancel/error
  // so callers can clean up partially-written files).
  result.materialized_entries = std::move(status.materialized_entries);
  cleanup_extract_overwrite_backups_best_effort(status.rollback_entries);

  if (const auto remapped_primary =
          primary_output_from_remaps(request, result.materialized_entries);
      remapped_primary.has_value()) {
    result.primary_output_path = remapped_primary->first;
    result.primary_is_directory = remapped_primary->second;
    return result;
  }

  // Compute primary_output_path for single-logical-entry requests.
  if (request.entries.size() == 1 && !result.materialized_entries.empty()) {
    const std::string norm_entry =
        normalize_archive_item_path(request.entries[0]);
    // First try: exact match on archive_entry_path.
    for (const auto& e : result.materialized_entries) {
      if (e.archive_entry_path == norm_entry) {
        result.primary_output_path = e.absolute_output_path;
        result.primary_is_directory = e.is_directory;
        break;
      }
    }
    // Second try: derived candidate path via output_dir + norm_entry.
    if (result.primary_output_path.empty()) {
      std::error_code ec;
      const std::string candidate =
          fs::absolute(fs::path(request.output_dir) / norm_entry, ec)
              .generic_string();
      if (!ec) {
        for (const auto& e : result.materialized_entries) {
          if (e.absolute_output_path == candidate) {
            result.primary_output_path = candidate;
            result.primary_is_directory = e.is_directory;
            break;
          }
        }
        // Third try: subtree — any child exists under norm_entry prefix.
        if (result.primary_output_path.empty()) {
          const std::string prefix = norm_entry + '/';
          for (const auto& e : result.materialized_entries) {
            if (e.archive_entry_path.size() > prefix.size() &&
                e.archive_entry_path.compare(0, prefix.size(), prefix) == 0) {
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

}  // namespace

ExtractResult NativeArchiveBackend::extract(const ExtractRequest& request,
                                            const ArchiveBackendHooks& hooks) {
  if (!request.archive_paths.empty()) {
    ExtractResult merged;
    bool has_any = false;
    for (const std::string& archive : request.archive_paths) {
      if (archive.empty()) {
        continue;
      }
      ExtractRequest single = request;
      single.archive_path = archive;
      single.archive_paths.clear();
      single.output_dir = resolve_multi_archive_output_dir(request.output_dir, archive);
      ExtractResult step = extract(single, hooks);
      if (!step.ok) {
        return step;
      }
      // Accumulate entries first, then overwrite merged with step's metadata.
      std::vector<ExtractMaterializedEntry> accumulated =
          std::move(merged.materialized_entries);
      for (auto& e : step.materialized_entries) {
        accumulated.push_back(std::move(e));
      }
      merged = std::move(step);
      merged.materialized_entries = std::move(accumulated);
      merged.primary_output_path.clear();
      merged.primary_is_directory = false;
      has_any = true;
    }
    if (!has_any) {
      return merged;
    }
    return merged;
  }

  // Token path: reuse an already-opened archive.
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<ExtractResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    const CArc* arc = archive_session_link(*session).GetArc();
    if (arc == nullptr || arc->Archive == nullptr) {
      return make_operation_failure<ExtractResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Session archive unavailable",
          7);
    }
    UInt32 num_items = 0;
    if (arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
      return make_operation_failure<ExtractResult>(
          ArchiveErrorDomain::kUnknown,
          "GetNumberOfItems failed",
          2);
    }
    ExtractRequest session_request = request;
    if (!session_request.password.empty()) {
      session->set_password(session_request.password);
    } else {
      session_request.password = session->password();
    }

    const ArchiveBackendHooks session_hooks =
        make_session_password_hooks(*session, hooks);
    ExtractResult result =
        run_extract_on_arc(arc,
                           num_items,
                           session_request,
                           session_hooks,
                           cancel_requested_,
                           [this]() { return this->wait_while_paused(); });
    if (!result.ok && result.error.domain == ArchiveErrorDomain::kPassword) {
      session->set_password({});
    }
    return result;
  }

  if (extract_open_requires_callback(request, hooks)) {
    OpenArchiveFromPathRequest open_request;
    open_request.archive_path = request.archive_path;
    open_request.archive_type_hint = request.archive_type_hint;
    const ArchiveBackendHooks open_hooks =
        hooks_with_extract_open_password(request, hooks);
    const OpenArchiveSessionResult opened =
        open_native_archive_session_from_path(
            ArchiveSessionRegistry::instance(),
            open_request,
            open_hooks,
            &cancel_requested_,
            [this]() { return this->wait_while_paused(); });
    if (!opened.ok) {
      return from_base_result<ExtractResult>(
          static_cast<OperationResult>(opened));
    }

    ExtractRequest session_request = request;
    session_request.session_token = opened.token;
    session_request.archive_path.clear();
    session_request.archive_type_hint.clear();
    ExtractResult result = extract(session_request, hooks);
    const OperationResult close_result =
        close_native_archive_session(ArchiveSessionRegistry::instance(),
                                     opened.token,
                                     hooks,
                                     &cancel_requested_,
                                     [this]() {
                                       return this->wait_while_paused();
                                     });
    if (result.ok && !close_result.ok) {
      return from_base_result<ExtractResult>(close_result);
    }
    return result;
  }

  return run_open_archive_read_pipeline<ExtractResult>(
      request.archive_path,
      request.archive_type_hint,
      hooks,
      false,
      [&](const OpenArchiveReadState& open_state, UInt32 num_items) -> ExtractResult {
        return run_extract_on_arc(open_state.arc,
                                   num_items,
                                   request,
                                   hooks,
                                   cancel_requested_,
                                   [this]() { return this->wait_while_paused(); });
      });
}

CompressionResourcesEstimate estimate_compression_resources_native(
    const AddRequest& request) {
  return estimate_compression_resources_for_request(request);
}

}  // namespace z7::app
