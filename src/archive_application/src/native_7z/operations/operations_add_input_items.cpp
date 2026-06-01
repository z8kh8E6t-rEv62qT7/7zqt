// src/archive_application/src/native_7z/operations/operations_add_input_items.cpp
// Role: Add request input_items validation and direct physical-to-archive mapping.

#include "operations/operations_add_input_items.h"

namespace z7::app {
namespace {

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

std::optional<OperationResult> validate_input_item_source(
    const fs::path& source_path) {
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
  return std::nullopt;
}

}  // namespace

std::optional<OperationResult> prepare_add_request_for_execution(
    const AddRequest& request,
    AddRequest* out_request) {
  if (out_request == nullptr) {
    return invalid_request("Add request preparation requires output storage");
  }

  AddRequest prepared = request;
  if (request.input_items.empty()) {
    *out_request = std::move(prepared);
    return std::nullopt;
  }

  prepared.input_paths.clear();
  prepared.input_items.clear();
  prepared.directory.clear();
  prepared.path_mode = "relative";

  std::unordered_set<std::string> seen_entries;
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

    if (std::optional<OperationResult> source_error =
            validate_input_item_source(fs::path(item.filesystem_path));
        source_error.has_value()) {
      return std::move(*source_error);
    }

    prepared.input_items.push_back(AddInputItem{item.filesystem_path,
                                                normalized_entry});
    prepared.input_paths.push_back(item.filesystem_path);
  }

  *out_request = std::move(prepared);
  return std::nullopt;
}

}  // namespace z7::app
