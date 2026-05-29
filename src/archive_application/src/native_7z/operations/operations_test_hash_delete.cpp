// src/archive_application/src/native_7z/operations/operations_test_hash_delete.cpp
// Role: Native backend test/hash/delete operations.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract.h"
#include "third_party_adapter/callbacks_update.h"
#include "session/session_registry_internal.h"

#include <algorithm>
#include <chrono>
#include <optional>

namespace z7::app {

namespace {

struct ScopedHashWorkspaceDir final {
  fs::path path;

  ~ScopedHashWorkspaceDir() {
    if (path.empty()) {
      return;
    }
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

fs::path make_hash_workspace_dir() {
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) {
    base = fs::current_path(ec);
  }
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path candidate =
      base / (std::string("z7-hash-") + std::to_string(static_cast<long long>(ticks)));
  for (int i = 0; i < 32; ++i) {
    std::error_code dir_ec;
    if (fs::create_directories(candidate, dir_ec)) {
      return candidate;
    }
    candidate =
        base /
        (std::string("z7-hash-") +
         std::to_string(static_cast<long long>(ticks + i + 1)));
  }
  return {};
}

void merge_test_hash_summary(HashSummary* merged, const HashSummary& step) {
  if (merged == nullptr) {
    return;
  }

  merged->num_archives += std::max<uint64_t>(1, step.num_archives);
  merged->num_dirs += step.num_dirs;
  merged->num_files += step.num_files;
  merged->files_size += step.files_size;
  merged->num_errors += step.num_errors;

  if (step.physical_size_defined) {
    merged->physical_size_defined = true;
    merged->physical_size += step.physical_size;
  }

  if (merged->main_name.empty() && !step.main_name.empty()) {
    merged->main_name = step.main_name;
  }
  if (merged->first_file_name.empty() && !step.first_file_name.empty()) {
    merged->first_file_name = step.first_file_name;
  }
}

uint64_t filesystem_input_size(const std::string& path) {
  std::error_code ec;
  const fs::path fs_path(path);
  if (!fs::is_regular_file(fs_path, ec)) {
    return 0;
  }
  const uintmax_t size = fs::file_size(fs_path, ec);
  if (ec) {
    return 0;
  }
  return static_cast<uint64_t>(size);
}

void append_directory_test_inputs(const fs::path& root,
                                  std::vector<std::string>* out) {
  if (out == nullptr) {
    return;
  }

  std::vector<std::string> entries;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      root,
      fs::directory_options::skip_permission_denied,
      ec);
  const fs::recursive_directory_iterator end;
  while (!ec && it != end) {
    const fs::directory_entry entry = *it;
    std::error_code status_ec;
    if (entry.is_regular_file(status_ec)) {
      entries.push_back(entry.path().string());
    }
    it.increment(ec);
  }

  std::sort(entries.begin(), entries.end());
  out->insert(out->end(), entries.begin(), entries.end());
}

std::vector<std::string> expand_filesystem_test_inputs(
    const std::vector<std::string>& inputs) {
  std::vector<std::string> expanded;
  for (const std::string& input : inputs) {
    if (input.empty()) {
      continue;
    }

    std::error_code ec;
    const fs::path path(input);
    if (fs::is_directory(path, ec)) {
      append_directory_test_inputs(path, &expanded);
      continue;
    }
    expanded.push_back(input);
  }
  return expanded;
}

std::vector<HashInputEntry> collect_archive_hash_entries(
    IInArchive* archive,
    UInt32 num_items,
    const std::vector<std::string>& requested_entries,
    std::string* single_selected_entry) {
  std::unordered_set<std::string> selected_entries;
  selected_entries.reserve(requested_entries.size());
  std::vector<std::string> normalized_request_entries;
  normalized_request_entries.reserve(requested_entries.size());
  for (const std::string& entry : requested_entries) {
    const std::string normalized = normalize_archive_item_path(entry);
    if (!normalized.empty() && selected_entries.insert(normalized).second) {
      normalized_request_entries.push_back(normalized);
    }
  }
  if (single_selected_entry != nullptr) {
    *single_selected_entry =
        normalized_request_entries.size() == 1 ? normalized_request_entries.front()
                                               : std::string();
  }

  std::vector<HashInputEntry> entries;
  entries.reserve(static_cast<size_t>(num_items));
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string item_path =
        archive_item_selection_path(archive, i);
    if (item_path.empty() ||
        !archive_path_matches_selection(item_path, selected_entries)) {
      continue;
    }

    bool is_dir = false;
    (void)archive_get_prop_bool(archive, i, kpidIsDir, is_dir);
    uint64_t size = 0;
    if (!is_dir) {
      (void)archive_get_prop_uint64(archive, i, kpidSize, size);
    }

    HashInputEntry entry;
    entry.relative_path = item_path;
    entry.is_dir = is_dir;
    entry.file_size = size;
    entries.push_back(std::move(entry));
  }
  return entries;
}

TestResult run_test_on_arc(const CArc* arc,
                          const TestRequest& request,
                          const ArchiveBackendHooks& hooks,
                          std::atomic<bool>& cancel_requested,
                          const std::function<bool()>& wait_while_paused,
                          const std::string& archive_display_path,
                          UInt32 num_items) {
  std::unordered_set<std::string> selected_entries;
  selected_entries.reserve(request.entries.size());
  std::vector<std::string> normalized_request_entries;
  normalized_request_entries.reserve(request.entries.size());
  for (const std::string& entry : request.entries) {
    const std::string normalized = normalize_archive_item_path(entry);
    if (!normalized.empty() && selected_entries.insert(normalized).second) {
      normalized_request_entries.push_back(normalized);
    }
  }

  TestArchiveItemStats item_stats;
  std::vector<UInt32> selected_indices;
  selected_indices.reserve(static_cast<size_t>(num_items));
  std::string first_matched_item_path;
  if (selected_entries.empty()) {
    item_stats = collect_test_archive_item_stats(arc->Archive, num_items);
  } else {
    for (UInt32 i = 0; i < num_items; ++i) {
      const std::string item_path =
          archive_item_selection_path(arc->Archive, i);
      if (!archive_path_matches_selection(item_path, selected_entries)) {
        continue;
      }
      if (first_matched_item_path.empty()) {
        first_matched_item_path = item_path;
      }
      selected_indices.push_back(i);
      accumulate_test_item_stats(arc->Archive, i, item_stats);
    }
  }

  const uint64_t selected_total_files = selected_entries.empty()
                                            ? static_cast<uint64_t>(num_items)
                                            : static_cast<uint64_t>(selected_indices.size());

  HashSummary test_summary;
  test_summary.num_archives = 1;
  test_summary.num_dirs = item_stats.num_dirs;
  test_summary.num_files = item_stats.num_files;
  test_summary.files_size = item_stats.total_unpacked_size;
  test_summary.physical_size_defined = arc->PhySize_Defined;
  test_summary.physical_size = arc->PhySize_Defined ? arc->PhySize : 0;
  if (!normalized_request_entries.empty()) {
    if (normalized_request_entries.size() == 1) {
      test_summary.main_name = normalized_request_entries.front();
    }
    if (item_stats.num_files == 1 && item_stats.num_dirs == 0) {
      if (!first_matched_item_path.empty()) {
        test_summary.first_file_name = first_matched_item_path;
      } else if (normalized_request_entries.size() == 1) {
        test_summary.first_file_name = normalized_request_entries.front();
      }
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

  emit_progress_event(hooks,
                      OperationStage::kRunning,
                      -1,
                      arc->PhySize_Defined,
                      arc->PhySize_Defined ? arc->PhySize : 0,
                      0,
                      selected_total_files,
                      0,
                      0,
                      {},
                      {});

  if (!selected_entries.empty() && selected_indices.empty()) {
    TestResult out = make_operation_success<TestResult>("There are no errors");
    out.hash_summary = test_summary;
    return out;
  }

  auto* callback = new NativeTestExtractCallback(arc->Archive,
                                                 hooks,
                                                 &cancel_requested,
                                                 wait_while_paused,
                                                 archive_display_path,
                                                 selected_total_files,
                                                 request.configured_memory_limit_bytes,
                                                 request.configured_memory_limit_defined);
  const UInt32* indices = nullptr;
  UInt32 num_indices = static_cast<UInt32>(-1);
  if (!selected_entries.empty()) {
    indices = selected_indices.data();
    num_indices = static_cast<UInt32>(selected_indices.size());
  }
  const ExtractInvocationStatus status = invoke_archive_extract_with_callback(
      arc->Archive, indices, num_indices, true, callback);

  return finalize_extract_operation_result<TestResult>(
      hooks,
      cancel_requested,
      selected_total_files,
      status,
      [&](const ExtractInvocationStatus& done) {
        TestResult out = done.diagnostic.empty()
                             ? make_operation_partial_success<TestResult>()
                             : make_operation_partial_success<TestResult>(done.diagnostic);
        test_summary.num_errors = done.error_count;
        out.hash_summary = test_summary;
        return out;
      },
      [&](const ExtractInvocationStatus&) {
        TestResult out = make_operation_success<TestResult>("There are no errors");
        out.hash_summary = test_summary;
        return out;
      });
}

}  // namespace

TestResult NativeArchiveBackend::test(const TestRequest& request,
                                      const ArchiveBackendHooks& hooks) {
  if (!request.archive_paths.empty()) {
    const std::vector<std::string> archive_inputs =
        expand_filesystem_test_inputs(request.archive_paths);
    TestResult merged;
    std::optional<TestResult> first_failure;
    HashSummary merged_summary;
    bool has_summary = false;
    bool has_any = false;
    uint64_t cumulative_error_count = 0;
    uint64_t display_completed_files = 0;
    uint64_t display_completed_bytes = 0;
    uint64_t display_total_bytes = 0;
    for (const std::string& archive : archive_inputs) {
      display_total_bytes += filesystem_input_size(archive);
    }
    auto emit_batch_snapshot = [&](const std::string& current_path) {
      emit_progress_event(hooks,
                          OperationStage::kRunning,
                          archive_inputs.empty()
                              ? -1
                              : static_cast<int>(
                                    (display_completed_files * 100) /
                                    static_cast<uint64_t>(archive_inputs.size())),
                          display_total_bytes != 0,
                          display_total_bytes,
                          display_completed_bytes,
                          static_cast<uint64_t>(archive_inputs.size()),
                          display_completed_files,
                          cumulative_error_count,
                          current_path,
                          {});
    };
    for (const std::string& archive : archive_inputs) {
      if (archive.empty()) {
        continue;
      }
      has_any = true;
      const uint64_t input_size = filesystem_input_size(archive);
      TestRequest single = request;
      single.archive_path = archive;
      single.archive_paths.clear();
      const TestResult step = test(single, hooks);
      ++display_completed_files;
      display_completed_bytes += input_size;
      if (!step.ok) {
        if (is_operation_canceled(step.error)) {
          return step;
        }
        const uint64_t step_error_count =
            step.hash_summary.has_value() && step.hash_summary->num_errors != 0
                ? step.hash_summary->num_errors
                : 1;
        cumulative_error_count += step_error_count;
        if (step.hash_summary.has_value()) {
          merge_test_hash_summary(&merged_summary, *step.hash_summary);
          has_summary = true;
        }
        const std::string reason = !step.summary.empty()
                                       ? step.summary
                                       : describe_archive_error(step.error);
        emit_log_event(hooks,
                       OperationStage::kRunning,
                       OutputChannel::kStdErr,
                       reason.empty() ? archive : archive + "\n" + reason);
        emit_batch_snapshot(archive);
        if (!first_failure.has_value()) {
          first_failure = step;
        }
        continue;
      }
      merged = step;
      if (step.hash_summary.has_value()) {
        merge_test_hash_summary(&merged_summary, *step.hash_summary);
        has_summary = true;
      }
      emit_batch_snapshot(archive);
    }
    if (!has_any) {
      return merged;
    }
    if (first_failure.has_value()) {
      if (has_summary || cumulative_error_count != 0) {
        merged_summary.num_errors =
            std::max(merged_summary.num_errors, cumulative_error_count);
        first_failure->hash_summary = merged_summary;
      }
      return *first_failure;
    }
    if (has_summary) {
      merged.hash_summary = merged_summary;
    }
    return merged;
  }

  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<TestResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    const CArc* arc = archive_session_link(*session).GetArc();
    UInt32 num_items = 0;
    const HRESULT num_items_hr = arc->Archive->GetNumberOfItems(&num_items);
    if (num_items_hr != S_OK) {
      return from_base_result<TestResult>(
          make_operation_failure_from_hresult<OperationResult>(num_items_hr));
    }
    return run_test_on_arc(arc,
                           request,
                           hooks,
                           cancel_requested_,
                           [this]() { return this->wait_while_paused(); },
                           session->display_path(),
                           num_items);
  }

  return run_open_archive_read_pipeline<TestResult>(
      request.archive_path,
      {},
      hooks,
      false,
      [&](const OpenArchiveReadState& open_state, UInt32 num_items) -> TestResult {
        return run_test_on_arc(open_state.arc,
                               request,
                               hooks,
                               cancel_requested_,
                               [this]() { return this->wait_while_paused(); },
                               request.archive_path,
                               num_items);
      });
}

// Delete/hash operations share the same unit as test flow.

HashResult NativeArchiveBackend::hash(const HashRequest& request,
                                      const ArchiveBackendHooks& hooks) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    emit_hash_progress(hooks, "Scanning", false, 0, 0, 0, 0, 0, {});

    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<HashResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    const CArc* arc = archive_session_link(*session).GetArc();
    if (arc == nullptr || arc->Archive == nullptr) {
      return make_operation_failure<HashResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Session archive unavailable",
          7);
    }

    UInt32 num_items = 0;
    const HRESULT num_items_hr = arc->Archive->GetNumberOfItems(&num_items);
    if (num_items_hr != S_OK) {
      return from_base_result<HashResult>(
          make_operation_failure_from_hresult<OperationResult>(num_items_hr));
    }

    std::string single_selected_entry;
    std::vector<HashInputEntry> hash_entries = collect_archive_hash_entries(
        arc->Archive,
        num_items,
        request.entries,
        &single_selected_entry);
    if (hash_entries.empty() && !request.entries.empty()) {
      return make_operation_failure<HashResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Hash request entries do not exist in archive",
          7);
    }

    const bool has_files = std::any_of(
        hash_entries.begin(),
        hash_entries.end(),
        [](const HashInputEntry& entry) { return !entry.is_dir; });
    ScopedHashWorkspaceDir workspace_dir;
    if (has_files) {
      workspace_dir.path = make_hash_workspace_dir();
      if (workspace_dir.path.empty()) {
        return make_operation_failure<HashResult>(
            ArchiveErrorDomain::kIo,
            "Failed to create temporary hash workspace",
            2);
      }

      ExtractRequest extract_request;
      extract_request.session_token = request.session_token;
      extract_request.output_dir = workspace_dir.path.string();
      extract_request.overwrite_mode = OverwriteMode::kOverwrite;
      extract_request.path_mode = ExtractPathMode::kFullPaths;
      extract_request.entries = request.entries;

      ArchiveBackendHooks extract_hooks = hooks;
      extract_hooks.on_event = {};
      const ExtractResult extract_result = extract(extract_request, extract_hooks);
      if (!extract_result.ok) {
        return from_base_result<HashResult>(
            static_cast<OperationResult>(extract_result));
      }

      for (HashInputEntry& entry : hash_entries) {
        if (entry.is_dir) {
          continue;
        }
        entry.absolute_path = workspace_dir.path / fs::path(entry.relative_path);
      }
    }

    return run_hash_entries(request, hooks, hash_entries, single_selected_entry);
  }
  return run_hash_internal(request, hooks);
}

DeleteResult NativeArchiveBackend::remove(const DeleteRequest& request,
                                          const ArchiveBackendHooks& hooks) {
  if (!request.filesystem_paths.empty()) {
    std::error_code ec;
    return run_filesystem_path_batch<DeleteResult>(
        request.filesystem_paths,
        hooks,
        cancel_requested_,
        [&](const std::string& path) -> std::optional<ArchiveError> {
          const bool deleted = request.use_recycle_bin
                                   ? move_path_to_recycle_bin(fs::path(path), ec)
                                   : remove_path_any(fs::path(path), ec);
          if (deleted) {
            return std::nullopt;
          }
          return make_archive_error(
              ArchiveErrorDomain::kIo,
              ec ? ec.message()
                 : (request.use_recycle_bin
                        ? std::string("Failed to move path to recycle bin")
                        : std::string("Failed to delete filesystem path")),
              2);
        },
        "Delete completed",
        [](DeleteResult&, uint64_t) {});
  }

  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<DeleteResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    if (!request.password.empty()) {
      session->set_password(request.password);
    }
    if (std::optional<OperationResult> materialize_error =
            ensure_archive_session_writable(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        materialize_error.has_value()) {
      return from_base_result<DeleteResult>(std::move(*materialize_error));
    }

    const ArchiveOpenSessionState& state = archive_session_state(*session);
    if (state.temp_file == nullptr || state.temp_file->empty()) {
      return make_operation_failure<DeleteResult>(
          ArchiveErrorDomain::kIo,
          "Writable archive session does not have a backing file",
          2);
    }

    DeleteRequest writable_request = request;
    writable_request.session_token.reset();
    writable_request.archive_path = state.temp_file->string();
    writable_request.password = session->password();

    DeleteResult delete_result = remove(writable_request, hooks);
    if (!delete_result.ok) {
      return delete_result;
    }
    ArchiveOpenSessionNativeAccess::set_dirty(*session, true);
    if (std::optional<OperationResult> refresh_error =
            refresh_archive_session_from_backing_file(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        refresh_error.has_value()) {
      return from_base_result<DeleteResult>(std::move(*refresh_error));
    }
    return delete_result;
  }

  return run_update_operation_with_mode<DeleteResult>(
      request.archive_path,
      hooks,
      static_cast<uint64_t>(request.entries.size()),
      [&]() {
        return NativeUpdateOperationCallback(
            hooks,
            &cancel_requested_,
            [this]() { return this->wait_while_paused(); },
            request.archive_path,
            NativeUpdateOperationCallback::Mode::kDelete,
            request.password);
      },
      [&](CCodecs&,
          CObjectVector<COpenType>&,
          NWildcard::CCensor& censor,
          CUpdateOptions& options) -> std::optional<OperationResult> {
        for (const std::string& entry : request.entries) {
          const std::string normalized = normalize_archive_item_path(entry);
          if (normalized.empty()) {
            continue;
          }
          censor.AddPreItem_NoWildcard(utf8_to_ustring(normalized));
        }

        options.Commands.Clear();
        CUpdateArchiveCommand command;
        command.ActionSet = NUpdateArchive::k_ActionSet_Delete;
        options.Commands.Add(command);
        options.ArcNameMode = k_ArcNameMode_Exact;
        return std::nullopt;
      });
}

}  // namespace z7::app
