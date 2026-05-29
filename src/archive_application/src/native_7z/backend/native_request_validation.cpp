// src/archive_application/src/native_7z/backend/native_request_validation.cpp
// Role: Private ArchiveRequest validation overloads.

#include "backend/native_request_validation.h"

#include "common/basename_validation.h"

namespace z7::app {
namespace {

bool is_absolute_extract_destination_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  const fs::path native_path(path);
  return native_path.is_absolute();
}

bool source_prefix_contains_path(const std::string& prefix,
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
}

int remap_specificity(const ExtractPathRemap& remap) {
  switch (remap.match_kind) {
    case ExtractPathRemapMatchKind::kExactArchivePath:
      return static_cast<int>(remap.source_path.size()) * 2 + 2;
    case ExtractPathRemapMatchKind::kArchivePrefix:
      return static_cast<int>(remap.source_path.size()) * 2 + 1;
    case ExtractPathRemapMatchKind::kRequestRoot:
    default:
      return 0;
  }
}

std::optional<OperationResult> validate_extract_path_remaps(
    const ExtractRequest& request) {
  if (request.path_remaps.empty()) {
    return std::nullopt;
  }

  for (const ExtractPathRemap& remap : request.path_remaps) {
    if (!is_absolute_extract_destination_path(remap.destination_path)) {
      return invalid_request(
          "Extract request path_remaps destination_path must be absolute");
    }

    const std::string normalized_source =
        normalize_archive_item_path(remap.source_path);
    switch (remap.match_kind) {
      case ExtractPathRemapMatchKind::kRequestRoot:
        if (!normalized_source.empty()) {
          return invalid_request(
              "Extract request kRequestRoot remap source_path must be empty");
        }
        if (!(request.entries.empty() || request.entries.size() == 1)) {
          return invalid_request(
              "Extract request kRequestRoot remap requires a single logical root");
        }
        break;
      case ExtractPathRemapMatchKind::kExactArchivePath:
        if (normalized_source.empty()) {
          return invalid_request(
              "Extract request kExactArchivePath remap requires source_path");
        }
        break;
      case ExtractPathRemapMatchKind::kArchivePrefix:
        if (normalized_source.empty()) {
          return invalid_request(
              "Extract request kArchivePrefix remap requires source_path");
        }
        break;
    }
  }

  for (size_t i = 0; i < request.path_remaps.size(); ++i) {
    for (size_t j = i + 1; j < request.path_remaps.size(); ++j) {
      const ExtractPathRemap& lhs = request.path_remaps[i];
      const ExtractPathRemap& rhs = request.path_remaps[j];
      if (lhs.match_kind != rhs.match_kind) {
        continue;
      }
      if (remap_specificity(lhs) != remap_specificity(rhs)) {
        continue;
      }

      const std::string lhs_source = normalize_archive_item_path(lhs.source_path);
      const std::string rhs_source = normalize_archive_item_path(rhs.source_path);

      if (lhs.match_kind == ExtractPathRemapMatchKind::kRequestRoot) {
        return invalid_request(
            "Extract request contains ambiguous kRequestRoot path remaps");
      }

      if (source_prefix_contains_path(lhs_source, rhs_source) ||
          source_prefix_contains_path(rhs_source, lhs_source)) {
        return invalid_request(
            "Extract request contains ambiguous path_remaps with equal specificity");
      }
    }
  }

  return std::nullopt;
}

std::optional<OperationResult> invalid_basename_only_name_request(
    std::string_view request_label,
    std::string_view field_label,
    z7::common::BasenameValidationError error) {
  switch (error) {
    case z7::common::BasenameValidationError::kEmpty:
      return invalid_request(std::string(request_label) + " requires " +
                             std::string(field_label));
    case z7::common::BasenameValidationError::kAbsolutePath:
      return invalid_request(std::string(request_label) + " " +
                             std::string(field_label) +
                             " must be a file name, not an absolute path");
    case z7::common::BasenameValidationError::kDotOrDotDot:
      return invalid_request(std::string(request_label) + " " +
                             std::string(field_label) +
                             " cannot be '.' or '..'");
    case z7::common::BasenameValidationError::kContainsPathSeparator:
      return invalid_request(std::string(request_label) + " " +
                             std::string(field_label) +
                             " must be a single file name, not a path");
  }
  return invalid_request(std::string(request_label) + " has invalid " +
                         std::string(field_label));
}

std::optional<OperationResult> validate_basename_only_name_field(
    std::string_view request_label,
    std::string_view field_label,
    const std::string& value) {
  const z7::common::BasenameValidationResult validation =
      z7::common::validate_basename_only_name(value);
  if (validation.ok) {
    return std::nullopt;
  }
  return invalid_basename_only_name_request(request_label, field_label,
                                            validation.error);
}

}  // namespace

std::optional<OperationResult> validate_request(const AddRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    const bool has_input_paths = !request.input_paths.empty();
    const bool has_input_items = !request.input_items.empty();
    if (!has_input_paths && !has_input_items) {
      return invalid_request("Add request requires inputs");
    }
    if (has_input_paths && has_input_items) {
      return invalid_request("Add request cannot mix input_paths and input_items");
    }
    if (has_input_items) {
      if (!request.directory.empty()) {
        return invalid_request("Add request with input_items cannot also set directory");
      }
      if (request.delete_after_compressing) {
        return invalid_request(
            "Add request with input_items does not support delete_after_compressing");
      }
      for (const AddInputItem& item : request.input_items) {
        if (item.filesystem_path.empty() || item.archive_entry.empty()) {
          return invalid_request(
              "Add request input_items require filesystem_path and archive_entry");
        }
      }
    }
    return std::nullopt;
  }

  const bool has_input_paths = !request.input_paths.empty();
  const bool has_input_items = !request.input_items.empty();
  if (request.archive_path.empty() || request.format.empty() ||
      (!has_input_paths && !has_input_items)) {
    return invalid_request("Add request requires archive path, format, and inputs");
  }
  if (has_input_paths && has_input_items) {
    return invalid_request("Add request cannot mix input_paths and input_items");
  }
  if (has_input_items) {
    if (!request.directory.empty()) {
      return invalid_request("Add request with input_items cannot also set directory");
    }
    if (request.delete_after_compressing) {
      return invalid_request(
          "Add request with input_items does not support delete_after_compressing");
    }
    for (const AddInputItem& item : request.input_items) {
      if (item.filesystem_path.empty() || item.archive_entry.empty()) {
        return invalid_request(
            "Add request input_items require filesystem_path and archive_entry");
      }
    }
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const ExtractRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    if (request.output_dir.empty()) {
      return invalid_request("Extract request requires output directory");
    }
    return validate_extract_path_remaps(request);
  }
  if (!request.archive_paths.empty()) {
    if (request.output_dir.empty()) {
      return invalid_request("Extract request requires archive path and output directory");
    }
    bool has_any_archive = false;
    for (const std::string& archive : request.archive_paths) {
      if (!archive.empty()) {
        has_any_archive = true;
        break;
      }
    }
    if (!has_any_archive) {
      return invalid_request("Extract request requires archive path and output directory");
    }
    return validate_extract_path_remaps(request);
  }
  if (request.archive_path.empty() || request.output_dir.empty()) {
    return invalid_request("Extract request requires archive path and output directory");
  }
  return validate_extract_path_remaps(request);
}

std::optional<OperationResult> validate_request(const TestRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    return std::nullopt;
  }
  if (!request.archive_paths.empty()) {
    bool has_any_archive = false;
    for (const std::string& archive : request.archive_paths) {
      if (!archive.empty()) {
        has_any_archive = true;
        break;
      }
    }
    if (!has_any_archive) {
      return invalid_request("Test request requires archive path");
    }
    return std::nullopt;
  }
  if (request.archive_path.empty()) {
    return invalid_request("Test request requires archive path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const BenchmarkRequest&) {
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const SplitRequest& request) {
  if (request.source_file_path.empty() || request.output_dir.empty() ||
      request.volume_size_spec.empty()) {
    return invalid_request("Split request requires source file, output dir, and volume size");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const CombineRequest& request) {
  if (request.source_part_path.empty() || request.output_dir.empty()) {
    return invalid_request("Combine request requires source part and output dir");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const HashRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    return std::nullopt;
  }
  if (request.input_paths.empty()) {
    return invalid_request("Hash request requires at least one input path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const DeleteRequest& request) {
  const bool has_filesystem_paths = !request.filesystem_paths.empty();
  const bool has_session_token =
      request.session_token.has_value() && request.session_token->is_valid();
  const bool has_archive_delete_fields =
      has_session_token || !request.archive_path.empty() || !request.entries.empty() ||
      !request.password.empty();

  if (has_filesystem_paths && has_archive_delete_fields) {
    return invalid_request(
        "Delete request cannot mix filesystem_paths with archive/session delete fields");
  }

  if (has_filesystem_paths) {
    return std::nullopt;
  }

  if (has_session_token) {
    if (request.entries.empty()) {
      return invalid_request("Delete request requires entries");
    }
    return std::nullopt;
  }
  if (request.filesystem_paths.empty() &&
      (request.archive_path.empty() || request.entries.empty())) {
    return invalid_request("Delete request requires archive path+entries or filesystem paths");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const OpenArchiveRequest& request) {
  if (request.archive_path.empty()) {
    return invalid_request("OpenArchive request requires archive path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const OpenArchiveFromPathRequest& request) {
  if (request.archive_path.empty()) {
    return invalid_request("OpenArchiveFromPath requires archive path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const OpenArchiveFromParentRequest& request) {
  if (!request.parent.is_valid()) {
    return invalid_request("OpenArchiveFromParent requires valid parent token");
  }
  const bool has_entry_path = !request.entry_path.empty();
  const bool has_entry_index = request.entry_index.has_value();
  if (!has_entry_path && !has_entry_index) {
    return invalid_request(
        "OpenArchiveFromParent requires exactly one selector");
  }
  if (has_entry_path && has_entry_index) {
    return invalid_request(
        "OpenArchiveFromParent accepts only one selector");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const CloseArchiveSessionRequest& request) {
  if (!request.token.is_valid()) {
    return invalid_request("CloseArchiveSession requires valid session token");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const ListRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    return std::nullopt;
  }
  if (request.archive_path.empty()) {
    return invalid_request("List request requires archive path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const ArchivePropertiesRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    return std::nullopt;
  }
  if (request.archive_path.empty()) {
    return invalid_request("Properties request requires archive path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const GetEntryInfoRequest& request) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    return std::nullopt;
  }
  if (request.archive_path.empty()) {
    return invalid_request("GetEntryInfo request requires archive path or session token");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const NavigateRequest& request) {
  if (request.to_path.empty()) {
    return invalid_request("Navigate request requires destination path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const CopyRequest& request) {
  if (request.source_paths.empty()) {
    return invalid_request("Copy request requires source paths");
  }
  if (!request.destination_path.empty() && request.source_paths.size() != 1) {
    return invalid_request("Copy request destination_path supports only one source");
  }
  if (request.destination_dir.empty() && request.destination_path.empty()) {
    return invalid_request("Copy request requires destination_dir or destination_path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const MoveRequest& request) {
  if (request.source_paths.empty()) {
    return invalid_request("Move request requires source paths");
  }
  if (!request.destination_path.empty() && request.source_paths.size() != 1) {
    return invalid_request("Move request destination_path supports only one source");
  }
  if (request.destination_dir.empty() && request.destination_path.empty()) {
    return invalid_request("Move request requires destination_dir or destination_path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const RenameRequest& request) {
  const bool has_session_token =
      request.session_token.has_value() && request.session_token->is_valid();
  const bool has_archive_fields =
      has_session_token || !request.archive_path.empty() || !request.entry_path.empty();

  if (has_archive_fields) {
    if (!request.source_path.empty()) {
      return invalid_request(
          "Rename request cannot mix filesystem source path with archive fields");
    }
    if (!has_session_token && request.archive_path.empty()) {
      return invalid_request("Rename request requires archive path or session token");
    }
    if (request.entry_path.empty() || request.new_name.empty()) {
      return invalid_request("Rename request requires entry path and new name");
    }
  } else if (request.source_path.empty() || request.new_name.empty()) {
    return invalid_request("Rename request requires source path and new name");
  }

  if (std::optional<OperationResult> error =
          validate_basename_only_name_field("Rename request", "new_name",
                                            request.new_name);
      error.has_value()) {
    return error;
  }
  if (has_archive_fields) {
    const std::string normalized_entry =
        normalize_archive_virtual_directory(request.entry_path);
    if (normalized_entry.empty()) {
      return invalid_request("Rename request entry path resolves to empty virtual path");
    }
    if (!archive_virtual_path_is_safe_for_materialization(normalized_entry)) {
      return invalid_request(
          "Rename request entry path contains unsafe path segment: " +
          normalized_entry);
    }
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(const CreateRequest& request) {
  if (request.parent_dir.empty() || request.name.empty()) {
    return invalid_request("Create request requires parent directory and name");
  }
  if (std::optional<OperationResult> error =
          validate_basename_only_name_field("Create request", "name",
                                            request.name);
      error.has_value()) {
    return error;
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const ArchiveCommentRequest& request) {
  const bool has_session_token =
      request.session_token.has_value() && request.session_token->is_valid();
  if (!has_session_token && request.archive_path.empty()) {
    return invalid_request(
        "Archive comment request requires archive path or session token");
  }
  if (request.entry_path.empty()) {
    return invalid_request("Archive comment request requires entry path");
  }
  return std::nullopt;
}

std::optional<OperationResult> validate_request(
    const FilesystemCommentRequest& request) {
  if (request.directory_path.empty() || request.entry_name.empty()) {
    return invalid_request(
        "Filesystem comment request requires directory path and entry name");
  }
  if (std::optional<OperationResult> error =
          validate_basename_only_name_field("Filesystem comment request",
                                            "entry_name",
                                            request.entry_name);
      error.has_value()) {
    return error;
  }
  return std::nullopt;
}

}  // namespace z7::app
