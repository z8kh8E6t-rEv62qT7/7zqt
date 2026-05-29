// src/archive_application/src/native_7z/operations/nested_writeback.cpp
// Role: Nested archive writeback pipeline shared by update/delete requests.

#include "operations/nested_writeback.h"

#include "common/archive_type_normalization.h"
#include "core/internal.h"
#include "core/filesystem_replace.h"

#include <chrono>

namespace z7::app {
namespace {

struct ScopedWorkspaceDir final {
  fs::path path;

  ScopedWorkspaceDir() = default;
  ScopedWorkspaceDir(const ScopedWorkspaceDir&) = delete;
  ScopedWorkspaceDir& operator=(const ScopedWorkspaceDir&) = delete;
  ScopedWorkspaceDir(ScopedWorkspaceDir&& other) noexcept
      : path(std::move(other.path)) {
    other.path.clear();
  }
  ScopedWorkspaceDir& operator=(ScopedWorkspaceDir&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    path = std::move(other.path);
    other.path.clear();
    return *this;
  }

  ~ScopedWorkspaceDir() {
    if (path.empty()) {
      return;
    }
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

struct NestedWritebackLayer final {
  fs::path archive_path;
  std::string child_entry;
};

struct NestedWritebackWorkspace final {
  ScopedWorkspaceDir workspace_dir;
  fs::path source_archive_path;
  fs::path root_working_archive_path;
  fs::path leaf_working_archive_path;
  std::vector<NestedWritebackLayer> parent_layers;
};

struct StagedWritebackInput final {
  fs::path staged_path;
  std::string relative_entry;
};

fs::path sibling_work_area_parent(const fs::path& path, std::error_code& ec) {
  ec.clear();
  if (path.has_parent_path()) {
    return path.parent_path();
  }
  return fs::current_path(ec);
}

bool ensure_sibling_work_area_writable(const fs::path& directory,
                                       std::error_code& ec) {
  ec.clear();
  if (directory.empty()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  const fs::path probe_path =
      directory / fs::path(".z7-writeback-probe-" + std::to_string(
                                               static_cast<long long>(
                                                   std::chrono::steady_clock::now()
                                                       .time_since_epoch()
                                                       .count())));
  {
    std::ofstream probe(probe_path, std::ios::binary | std::ios::trunc);
    if (!probe.is_open()) {
      ec = std::make_error_code(std::errc::permission_denied);
      return false;
    }
  }

  fs::remove(probe_path, ec);
  return !ec;
}

fs::path make_nested_writeback_workspace_dir(const fs::path& source_archive_path,
                                             std::error_code& ec) {
  const fs::path base = sibling_work_area_parent(source_archive_path, ec);
  if (ec || base.empty()) {
    return {};
  }
  if (!ensure_sibling_work_area_writable(base, ec)) {
    return {};
  }

  const std::string archive_name = source_archive_path.filename().string();
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int i = 0; i < 32; ++i) {
    const fs::path candidate =
        base /
        (archive_name + ".z7-writeback-" +
         std::to_string(static_cast<long long>(ticks + i)));
    std::error_code dir_ec;
    if (fs::create_directories(candidate, dir_ec)) {
      ec.clear();
      return candidate;
    }
  }

  ec = std::make_error_code(std::errc::file_exists);
  return {};
}

std::string update_format_from_archive_path(const fs::path& archive_path) {
  std::string ext =
      z7::common::to_lower_ascii_copy(archive_path.extension().string());
  if (!ext.empty() && ext.front() == '.') {
    ext.erase(ext.begin());
  }
  if (ext == "*" || ext == "#") {
    return {};
  }
  return z7::common::canonical_archive_type_from_filename_suffix_copy(ext);
}

std::optional<OperationResult> stage_parent_writeback_input(
    const NestedWritebackWorkspace& workspace,
    size_t stage_index,
    const fs::path& child_archive_path,
    const std::string& child_entry,
    StagedWritebackInput* out_input) {
  if (out_input == nullptr) {
    return invalid_request("Nested writeback staged input output is required");
  }

  const std::string staged_relative_entry =
      normalize_archive_virtual_directory(child_entry);
  if (staged_relative_entry.empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Nested archive writeback lost the staged child archive entry path",
        2);
  }

  StagedWritebackInput input;
  const fs::path staging_root =
      workspace.workspace_dir.path /
      (std::string("parent-stage-") + std::to_string(stage_index));
  input.relative_entry = staged_relative_entry;

  std::error_code stage_dir_ec;
  fs::create_directories(staging_root, stage_dir_ec);
  if (stage_dir_ec) {
    return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                   stage_dir_ec.message(),
                                                   2);
  }

  input.staged_path = staging_root / fs::path(input.relative_entry);
  const fs::path staged_parent_path = input.staged_path.parent_path();
  if (!staged_parent_path.empty()) {
    std::error_code stage_parent_ec;
    fs::create_directories(staged_parent_path, stage_parent_ec);
    if (stage_parent_ec) {
      return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                     stage_parent_ec.message(),
                                                     2);
    }
  }

  std::error_code copy_ec;
  fs::copy_file(child_archive_path,
                input.staged_path,
                fs::copy_options::overwrite_existing,
                copy_ec);
  if (copy_ec) {
    return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                   copy_ec.message(),
                                                   2);
  }

  *out_input = std::move(input);
  return std::nullopt;
}

AddResult run_add_from_staged_tree(NativeArchiveBackend& backend,
                                   const fs::path& archive_path,
                                   const std::string& archive_format,
                                   const StagedWritebackInput& input,
                                   const ArchiveBackendHooks& hooks) {
  AddRequest update_request;
  update_request.archive_path = archive_path.string();
  update_request.format = archive_format;
  update_request.update_mode = "update";
  update_request.input_items.push_back(
      AddInputItem{input.staged_path.string(), input.relative_entry});
  return backend.add(update_request, hooks);
}

std::optional<OperationResult> prepare_nested_writeback_workspace(
    NativeArchiveBackend& backend,
    const std::string& source_archive_path,
    const std::vector<std::string>& nested_archive_entries,
    const ArchiveBackendHooks& hooks,
    NestedWritebackWorkspace* out_workspace) {
  if (out_workspace == nullptr) {
    return invalid_request("Nested writeback workspace output is required");
  }
  if (source_archive_path.empty() || nested_archive_entries.empty()) {
    return invalid_request("Nested writeback requires archive path and embedded archive chain");
  }

  NestedWritebackWorkspace workspace;
  workspace.source_archive_path = fs::path(source_archive_path);
  std::error_code workspace_ec;
  workspace.workspace_dir.path =
      make_nested_writeback_workspace_dir(workspace.source_archive_path,
                                          workspace_ec);
  if (workspace.workspace_dir.path.empty()) {
    const std::string message =
        workspace_ec
            ? "Failed to create nested writeback workspace: " +
                  workspace_ec.message()
            : "Failed to create nested writeback workspace";
    return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                   message,
                                                   2);
  }
  workspace.root_working_archive_path =
      workspace.workspace_dir.path / workspace.source_archive_path.filename();

  std::error_code copy_ec;
  fs::copy_file(workspace.source_archive_path,
                workspace.root_working_archive_path,
                fs::copy_options::overwrite_existing,
                copy_ec);
  if (copy_ec) {
    return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                   copy_ec.message(),
                                                   2);
  }

  fs::path current_archive_path = workspace.root_working_archive_path;
  workspace.parent_layers.reserve(nested_archive_entries.size());
  for (size_t index = 0; index < nested_archive_entries.size(); ++index) {
    const std::string normalized_entry =
        normalize_archive_virtual_directory(nested_archive_entries[index]);
    if (normalized_entry.empty()) {
      return invalid_request("Nested archive writeback chain contains an empty entry");
    }
    if (!archive_virtual_path_is_safe_for_materialization(normalized_entry)) {
      return invalid_request(
          "Nested archive writeback chain contains unsafe path segment: " +
          normalized_entry);
    }

    const fs::path output_dir =
        workspace.workspace_dir.path / (std::string("layer-") + std::to_string(index));
    std::error_code mkdir_ec;
    fs::create_directories(output_dir, mkdir_ec);
    if (mkdir_ec) {
      return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                     mkdir_ec.message(),
                                                     2);
    }

    ExtractRequest extract_request;
    extract_request.archive_path = current_archive_path.string();
    extract_request.output_dir = output_dir.string();
    extract_request.overwrite_mode = OverwriteMode::kOverwrite;
    extract_request.entries.push_back(normalized_entry);

    const ExtractResult extract_result = backend.extract(extract_request, hooks);
    if (!extract_result.ok) {
      return static_cast<OperationResult>(extract_result);
    }

    const fs::path extracted_child_path = output_dir / fs::path(normalized_entry);
    std::error_code stat_ec;
    const bool is_file = fs::is_regular_file(extracted_child_path, stat_ec);
    if (stat_ec || !is_file) {
      return make_operation_failure<OperationResult>(
          ArchiveErrorDomain::kIo,
          "Nested archive writeback failed to materialize embedded archive: " +
              normalized_entry,
          2);
    }

    workspace.parent_layers.push_back(
        NestedWritebackLayer{current_archive_path, normalized_entry});
    current_archive_path = extracted_child_path;
  }

  workspace.leaf_working_archive_path = current_archive_path;
  *out_workspace = std::move(workspace);
  return std::nullopt;
}

std::optional<OperationResult> propagate_nested_writeback_to_root(
    NativeArchiveBackend& backend,
    const NestedWritebackWorkspace& workspace,
    const ArchiveBackendHooks& hooks) {
  fs::path child_archive_path = workspace.leaf_working_archive_path;
  size_t stage_index = 0;
  for (auto it = workspace.parent_layers.rbegin();
       it != workspace.parent_layers.rend();
       ++it, ++stage_index) {
    const std::string parent_format = update_format_from_archive_path(it->archive_path);
    if (parent_format.empty()) {
      return make_operation_failure<OperationResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Cannot determine parent archive format for nested writeback: " +
              it->archive_path.string(),
          7);
    }

    StagedWritebackInput input;
    if (std::optional<OperationResult> stage_error =
            stage_parent_writeback_input(
                workspace,
                stage_index,
                child_archive_path,
                it->child_entry,
                &input);
        stage_error.has_value()) {
      return std::move(*stage_error);
    }

    const AddResult update_result = run_add_from_staged_tree(
        backend,
        it->archive_path,
        parent_format,
        input,
        hooks);
    if (!update_result.ok) {
      return static_cast<OperationResult>(update_result);
    }

    child_archive_path = it->archive_path;
  }

  const AtomicReplaceResult replace_result = replace_file_atomically(
      workspace.root_working_archive_path,
      workspace.source_archive_path,
      ".z7-writeback-backup-");
  if (replace_result.success) {
    return std::nullopt;
  }
  return replace_result.error;
}

template <typename TResult, typename LeafFn>
TResult run_nested_writeback(NativeArchiveBackend& backend,
                             const std::string& source_archive_path,
                             const std::vector<std::string>& nested_archive_entries,
                             const ArchiveBackendHooks& hooks,
                             LeafFn&& run_leaf_operation,
                             const std::string& success_summary) {
  try {
    emit_log_event(hooks,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Preparing nested archive writeback");

    NestedWritebackWorkspace workspace;
    if (std::optional<OperationResult> prepare_error =
            prepare_nested_writeback_workspace(
                backend,
                source_archive_path,
                nested_archive_entries,
                hooks,
                &workspace);
        prepare_error.has_value()) {
      return from_base_result<TResult>(std::move(*prepare_error));
    }

    TResult leaf_result = run_leaf_operation(workspace.leaf_working_archive_path.string());
    if (!leaf_result.ok) {
      return leaf_result;
    }

    emit_log_event(hooks,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Writing nested archive changes back to parent archive");
    if (std::optional<OperationResult> writeback_error =
            propagate_nested_writeback_to_root(backend, workspace, hooks);
        writeback_error.has_value()) {
      return from_base_result<TResult>(std::move(*writeback_error));
    }

    static_cast<OperationResult&>(leaf_result) =
        make_operation_success<OperationResult>(success_summary);
    return leaf_result;
  } catch (const std::exception& ex) {
    return make_operation_failure<TResult>(ArchiveErrorDomain::kUnknown,
                                          std::string("Nested archive writeback failed: ") +
                                              ex.what(),
                                          2);
  } catch (...) {
    return make_operation_failure<TResult>(ArchiveErrorDomain::kUnknown,
                                          "Nested archive writeback failed with unknown exception",
                                          2);
  }
}

}  // namespace

AddResult run_add_with_nested_writeback(NativeArchiveBackend& backend,
                                        const AddRequest& request,
                                        const ArchiveBackendHooks& hooks) {
  return run_nested_writeback<AddResult>(
      backend,
      request.archive_path,
      request.nested_archive_entries,
      hooks,
      [&](const std::string& leaf_archive_path) {
        AddRequest leaf_request = request;
        leaf_request.archive_path = leaf_archive_path;
        leaf_request.nested_archive_entries.clear();
        return backend.add(leaf_request, hooks);
      },
      "Archive updated");
}

DeleteResult run_delete_with_nested_writeback(NativeArchiveBackend& backend,
                                              const DeleteRequest& request,
                                              const ArchiveBackendHooks& hooks) {
  return run_nested_writeback<DeleteResult>(
      backend,
      request.archive_path,
      request.nested_archive_entries,
      hooks,
      [&](const std::string& leaf_archive_path) {
        DeleteRequest leaf_request = request;
        leaf_request.archive_path = leaf_archive_path;
        leaf_request.nested_archive_entries.clear();
        return backend.remove(leaf_request, hooks);
      },
      "Delete completed");
}

}  // namespace z7::app
