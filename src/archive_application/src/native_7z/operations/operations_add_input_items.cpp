// src/archive_application/src/native_7z/operations/operations_add_input_items.cpp
// Role: Add request input_items staging and materialization.

#include "operations/operations_add_input_items.h"

namespace z7::app {
namespace {

fs::path make_add_input_tree_dir() {
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) {
    base = fs::current_path(ec);
  }

  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path candidate =
      base / (std::string("z7-add-items-") + std::to_string(static_cast<long long>(ticks)));
  for (int i = 0; i < 32; ++i) {
    std::error_code dir_ec;
    if (fs::create_directories(candidate, dir_ec)) {
      return candidate;
    }
    candidate =
        base /
        (std::string("z7-add-items-") +
         std::to_string(static_cast<long long>(ticks + i + 1)));
  }
  return {};
}

std::string first_archive_path_component(const std::string& entry) {
  const size_t slash = entry.find('/');
  return slash == std::string::npos ? entry : entry.substr(0, slash);
}

std::optional<OperationResult> make_io_failure(const std::error_code& ec,
                                               const std::string& context) {
  std::string summary = context;
  if (ec) {
    if (!summary.empty()) {
      summary += ": ";
    }
    summary += ec.message();
  }
  return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                 std::move(summary),
                                                 2);
}

std::optional<OperationResult> materialize_add_input_item(
    const fs::path& source_path,
    const fs::path& destination_path,
    const std::string& archive_entry);

std::optional<OperationResult> materialize_add_input_directory(
    const fs::path& source_dir,
    const fs::path& destination_dir,
    const std::string& archive_entry) {
  std::error_code ec;
  const bool destination_exists = fs::exists(destination_dir, ec);
  if (ec) {
    return make_io_failure(ec,
                           "Failed to inspect add input destination: " +
                               destination_dir.string());
  }
  if (destination_exists) {
    const bool destination_is_dir = fs::is_directory(destination_dir, ec);
    if (ec) {
      return make_io_failure(ec,
                             "Failed to inspect add input destination type: " +
                                 destination_dir.string());
    }
    if (!destination_is_dir) {
      return invalid_request(
          "Add request input_items path conflicts with an existing file: " + archive_entry);
    }
  } else {
    fs::create_directories(destination_dir, ec);
    if (ec) {
      return make_io_failure(ec,
                             "Failed to create add input destination directory: " +
                                 destination_dir.string());
    }
  }

  fs::directory_iterator iter(source_dir, ec);
  if (ec) {
    return make_io_failure(ec,
                           "Failed to enumerate add input directory: " +
                               source_dir.string());
  }
  for (const fs::directory_entry& child : iter) {
    const std::string child_entry = archive_entry.empty()
                                        ? child.path().filename().string()
                                        : archive_entry + "/" + child.path().filename().string();
    if (std::optional<OperationResult> child_error =
            materialize_add_input_item(child.path(),
                                       destination_dir / child.path().filename(),
                                       child_entry);
        child_error.has_value()) {
      return std::move(*child_error);
    }
  }

  return std::nullopt;
}

std::optional<OperationResult> materialize_add_input_item(
    const fs::path& source_path,
    const fs::path& destination_path,
    const std::string& archive_entry) {
  std::error_code ec;
  const bool source_exists = fs::exists(source_path, ec);
  if (ec) {
    return make_io_failure(ec,
                           "Failed to inspect add input source: " +
                               source_path.string());
  }
  if (!source_exists) {
    return invalid_request("Add request input_items source does not exist: " +
                           source_path.string());
  }

  const bool source_is_dir = fs::is_directory(source_path, ec);
  if (ec) {
    return make_io_failure(ec,
                           "Failed to inspect add input source type: " +
                               source_path.string());
  }
  if (source_is_dir) {
    return materialize_add_input_directory(source_path, destination_path, archive_entry);
  }

  const bool destination_exists = fs::exists(destination_path, ec);
  if (ec) {
    return make_io_failure(ec,
                           "Failed to inspect add input destination: " +
                               destination_path.string());
  }
  if (destination_exists) {
    const bool destination_is_dir = fs::is_directory(destination_path, ec);
    if (ec) {
      return make_io_failure(ec,
                             "Failed to inspect add input destination type: " +
                                 destination_path.string());
    }
    if (destination_is_dir) {
      return invalid_request(
          "Add request input_items path conflicts with an existing directory: " +
          archive_entry);
    }
  }

  if (!ensure_parent_dir(destination_path, ec)) {
    return make_io_failure(ec,
                           "Failed to create add input destination parent: " +
                               destination_path.string());
  }
  if (!copy_path_any(source_path, destination_path, true, ec)) {
    return make_io_failure(ec,
                           "Failed to materialize add input item: " +
                               source_path.string());
  }
  return std::nullopt;
}

}  // namespace

ScopedAddInputTree::~ScopedAddInputTree() {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  fs::remove_all(path, ec);
}

std::optional<OperationResult> prepare_add_request_for_execution(
    const AddRequest& request,
    ScopedAddInputTree* staging_tree,
    AddRequest* out_request) {
  if (staging_tree == nullptr || out_request == nullptr) {
    return invalid_request("Add request preparation requires output storage");
  }

  AddRequest prepared = request;
  if (request.input_items.empty()) {
    *out_request = std::move(prepared);
    return std::nullopt;
  }

  staging_tree->path = make_add_input_tree_dir();
  if (staging_tree->path.empty()) {
    return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                   "Failed to create add input staging tree",
                                                   2);
  }

  prepared.input_paths.clear();
  prepared.input_items.clear();
  prepared.directory.clear();
  prepared.path_mode = "relative";

  std::unordered_set<std::string> seen_entries;
  std::unordered_set<std::string> seen_top_levels;
  for (const AddInputItem& item : request.input_items) {
    const std::string normalized_entry =
        normalize_archive_virtual_directory(item.archive_entry);
    if (normalized_entry.empty()) {
      return invalid_request("Add request input_items archive_entry must not be empty");
    }
    if (!archive_virtual_path_is_safe_for_materialization(normalized_entry)) {
      return invalid_request(
          "Add request input_items archive_entry contains unsafe path segment: " +
          normalized_entry);
    }
    if (!seen_entries.insert(normalized_entry).second) {
      return invalid_request("Add request input_items contain duplicate archive_entry: " +
                             normalized_entry);
    }

    if (std::optional<OperationResult> stage_error =
            materialize_add_input_item(fs::path(item.filesystem_path),
                                       staging_tree->path / fs::path(normalized_entry),
                                       normalized_entry);
        stage_error.has_value()) {
      return std::move(*stage_error);
    }

    const std::string top_level = first_archive_path_component(normalized_entry);
    if (!seen_top_levels.insert(top_level).second) {
      continue;
    }
    prepared.input_paths.push_back((staging_tree->path / fs::path(top_level)).string());
  }

  *out_request = std::move(prepared);
  return std::nullopt;
}

}  // namespace z7::app
