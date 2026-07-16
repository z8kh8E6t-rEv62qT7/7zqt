// src/archive_application/src/native_7z/backend/native_request_validation.cpp
// Role: Private ArchiveRequest validation overloads.

#include "backend/native_request_validation.h"

#include "common/basename_validation.h"

namespace z7::app {
    namespace {

        bool is_absolute_extract_destination_path(std::string const& path) {
            if (path.empty()) {
                return false;
            }
            fs::path const native_path(path);
            return native_path.is_absolute();
        }

        bool source_prefix_contains_path(std::string const& prefix, std::string const& path) {
            if (prefix.empty()) {
                return true;
            }
            if (path == prefix) {
                return true;
            }
            return path.size() > prefix.size()
                && path.compare(0, prefix.size(), prefix) == 0
                && path[prefix.size()] == '/';
        }

        int remap_specificity(ExtractPathRemap const& remap) {
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

        std::optional<OperationResult> validate_extract_path_remaps(ExtractRequest const& request) {
            if (request.path_remaps.empty()) {
                return std::nullopt;
            }

            for (ExtractPathRemap const& remap : request.path_remaps) {
                if (!is_absolute_extract_destination_path(remap.destination_path)) {
                    return invalid_request("Extract request path_remaps destination_path must be absolute");
                }

                std::string const normalized_source = normalize_archive_item_path(remap.source_path);
                switch (remap.match_kind) {
                    case ExtractPathRemapMatchKind::kRequestRoot:
                        if (!normalized_source.empty()) {
                            return invalid_request("Extract request kRequestRoot remap source_path must be empty");
                        }
                        if (!(request.entries.empty() || request.entries.size() == 1)) {
                            return invalid_request("Extract request kRequestRoot remap requires a single logical root");
                        }
                        break;
                    case ExtractPathRemapMatchKind::kExactArchivePath:
                        if (normalized_source.empty()) {
                            return invalid_request("Extract request kExactArchivePath remap requires source_path");
                        }
                        break;
                    case ExtractPathRemapMatchKind::kArchivePrefix:
                        if (normalized_source.empty()) {
                            return invalid_request("Extract request kArchivePrefix remap requires source_path");
                        }
                        break;
                }
            }

            for (size_t i = 0; i < request.path_remaps.size(); ++i) {
                for (size_t j = i + 1; j < request.path_remaps.size(); ++j) {
                    ExtractPathRemap const& lhs = request.path_remaps[i];
                    ExtractPathRemap const& rhs = request.path_remaps[j];
                    if (lhs.match_kind != rhs.match_kind) {
                        continue;
                    }
                    if (remap_specificity(lhs) != remap_specificity(rhs)) {
                        continue;
                    }

                    std::string const lhs_source = normalize_archive_item_path(lhs.source_path);
                    std::string const rhs_source = normalize_archive_item_path(rhs.source_path);

                    if (lhs.match_kind == ExtractPathRemapMatchKind::kRequestRoot) {
                        return invalid_request("Extract request contains ambiguous kRequestRoot path remaps");
                    }

                    if (source_prefix_contains_path(lhs_source, rhs_source)
                        || source_prefix_contains_path(rhs_source, lhs_source)) {
                        return invalid_request("Extract request contains ambiguous path_remaps with equal specificity");
                    }
                }
            }

            return std::nullopt;
        }

        std::optional<OperationResult> invalid_basename_only_name_request(std::string_view request_label,
                                                                          std::string_view field_label,
                                                                          z7::common::BasenameValidationError error) {
            switch (error) {
                case z7::common::BasenameValidationError::kEmpty:
                    return invalid_request(std::string(request_label) + " requires " + std::string(field_label));
                case z7::common::BasenameValidationError::kAbsolutePath:
                    return invalid_request(std::string(request_label)
                                           + " "
                                           + std::string(field_label)
                                           + " must be a file name, not an absolute path");
                case z7::common::BasenameValidationError::kDotOrDotDot:
                    return invalid_request(
                        std::string(request_label) + " " + std::string(field_label) + " cannot be '.' or '..'");
                case z7::common::BasenameValidationError::kContainsPathSeparator:
                    return invalid_request(std::string(request_label)
                                           + " "
                                           + std::string(field_label)
                                           + " must be a single file name, not a path");
            }
            return invalid_request(std::string(request_label) + " has invalid " + std::string(field_label));
        }

        std::optional<OperationResult> validate_basename_only_name_field(std::string_view request_label,
                                                                         std::string_view field_label,
                                                                         std::string const& value) {
            z7::common::BasenameValidationResult const validation = z7::common::validate_basename_only_name(value);
            if (validation.ok) {
                return std::nullopt;
            }
            return invalid_basename_only_name_request(request_label, field_label, validation.error);
        }

    } // namespace

    std::optional<OperationResult> validate_request(AddRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            bool const has_input_paths = !request.input_paths.empty();
            bool const has_input_items = !request.input_items.empty();
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
                    return invalid_request("Add request with input_items does not support delete_after_compressing");
                }
                for (AddInputItem const& item : request.input_items) {
                    if (item.filesystem_path.empty() || item.archive_entry.empty()) {
                        return invalid_request("Add request input_items require filesystem_path and archive_entry");
                    }
                }
            }
            return std::nullopt;
        }

        bool const has_input_paths = !request.input_paths.empty();
        bool const has_input_items = !request.input_items.empty();
        if (request.archive_path.empty() || request.format.empty() || (!has_input_paths && !has_input_items)) {
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
                return invalid_request("Add request with input_items does not support delete_after_compressing");
            }
            for (AddInputItem const& item : request.input_items) {
                if (item.filesystem_path.empty() || item.archive_entry.empty()) {
                    return invalid_request("Add request input_items require filesystem_path and archive_entry");
                }
            }
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(ExtractRequest const& request) {
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
            for (std::string const& archive : request.archive_paths) {
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

    std::optional<OperationResult> validate_request(TestRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            return std::nullopt;
        }
        if (!request.archive_paths.empty()) {
            bool has_any_archive = false;
            for (std::string const& archive : request.archive_paths) {
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

    std::optional<OperationResult> validate_request(BenchmarkRequest const&) {
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(SplitRequest const& request) {
        if (request.source_file_path.empty() || request.output_dir.empty() || request.volume_size_spec.empty()) {
            return invalid_request("Split request requires source file, output dir, and volume size");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(CombineRequest const& request) {
        if (request.source_part_path.empty() || request.output_dir.empty()) {
            return invalid_request("Combine request requires source part and output dir");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(HashRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            return std::nullopt;
        }
        if (request.input_paths.empty()) {
            return invalid_request("Hash request requires at least one input path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(DeleteRequest const& request) {
        bool const has_filesystem_paths = !request.filesystem_paths.empty();
        bool const has_session_token = request.session_token.has_value() && request.session_token->is_valid();
        bool const has_archive_delete_fields =
            has_session_token || !request.archive_path.empty() || !request.entries.empty() || !request.password.empty();

        if (has_filesystem_paths && has_archive_delete_fields) {
            return invalid_request("Delete request cannot mix filesystem_paths with archive/session delete fields");
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
        if (request.filesystem_paths.empty() && (request.archive_path.empty() || request.entries.empty())) {
            return invalid_request("Delete request requires archive path+entries or filesystem paths");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(OpenArchiveRequest const& request) {
        if (request.archive_path.empty()) {
            return invalid_request("OpenArchive request requires archive path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(OpenArchiveFromPathRequest const& request) {
        if (request.archive_path.empty()) {
            return invalid_request("OpenArchiveFromPath requires archive path");
        }
        if (request.filename_code_page.has_value()
            && !is_filename_code_page_supported(*request.filename_code_page)) {
            return invalid_request("unsupported filename code page");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(OpenArchiveFromParentRequest const& request) {
        if (!request.parent.is_valid()) {
            return invalid_request("OpenArchiveFromParent requires valid parent token");
        }
        bool const has_entry_path = !request.entry_path.empty();
        bool const has_entry_index = request.entry_index.has_value();
        if (!has_entry_path && !has_entry_index) {
            return invalid_request("OpenArchiveFromParent requires exactly one selector");
        }
        if (has_entry_path && has_entry_index) {
            return invalid_request("OpenArchiveFromParent accepts only one selector");
        }
        if (request.filename_code_page.has_value()
            && !is_filename_code_page_supported(*request.filename_code_page)) {
            return invalid_request("unsupported filename code page");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(SetArchiveSessionFilenameCodePageRequest const& request) {
        if (!request.token.is_valid()) {
            return invalid_request("SetArchiveSessionFilenameCodePage requires valid session token");
        }
        if (request.filename_code_page.has_value()
            && !is_filename_code_page_supported(*request.filename_code_page)) {
            return invalid_request("unsupported filename code page");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(CloseArchiveSessionRequest const& request) {
        if (!request.token.is_valid()) {
            return invalid_request("CloseArchiveSession requires valid session token");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(ListRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            return std::nullopt;
        }
        if (request.archive_path.empty()) {
            return invalid_request("List request requires archive path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(ArchivePropertiesRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            return std::nullopt;
        }
        if (request.archive_path.empty()) {
            return invalid_request("Properties request requires archive path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(GetEntryInfoRequest const& request) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            return std::nullopt;
        }
        if (request.archive_path.empty()) {
            return invalid_request("GetEntryInfo request requires archive path or session token");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(NavigateRequest const& request) {
        if (request.to_path.empty()) {
            return invalid_request("Navigate request requires destination path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(CopyRequest const& request) {
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

    std::optional<OperationResult> validate_request(MoveRequest const& request) {
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

    std::optional<OperationResult> validate_request(RenameRequest const& request) {
        bool const has_session_token = request.session_token.has_value() && request.session_token->is_valid();
        bool const has_archive_fields =
            has_session_token || !request.archive_path.empty() || !request.entry_path.empty();

        if (has_archive_fields) {
            if (!request.source_path.empty()) {
                return invalid_request("Rename request cannot mix filesystem source path with archive fields");
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
                validate_basename_only_name_field("Rename request", "new_name", request.new_name);
            error.has_value()) {
            return error;
        }
        if (has_archive_fields) {
            std::string const normalized_entry = normalize_archive_virtual_directory(request.entry_path);
            if (normalized_entry.empty()) {
                return invalid_request("Rename request entry path resolves to empty virtual path");
            }
            if (!archive_virtual_path_is_safe_for_materialization(normalized_entry)) {
                return invalid_request("Rename request entry path contains unsafe path segment: " + normalized_entry);
            }
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(CreateRequest const& request) {
        if (request.parent_dir.empty() || request.name.empty()) {
            return invalid_request("Create request requires parent directory and name");
        }
        if (std::optional<OperationResult> error =
                validate_basename_only_name_field("Create request", "name", request.name);
            error.has_value()) {
            return error;
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(ArchiveCommentRequest const& request) {
        bool const has_session_token = request.session_token.has_value() && request.session_token->is_valid();
        if (!has_session_token && request.archive_path.empty()) {
            return invalid_request("Archive comment request requires archive path or session token");
        }
        if (request.entry_path.empty()) {
            return invalid_request("Archive comment request requires entry path");
        }
        return std::nullopt;
    }

    std::optional<OperationResult> validate_request(FilesystemCommentRequest const& request) {
        if (request.directory_path.empty() || request.entry_name.empty()) {
            return invalid_request("Filesystem comment request requires directory path and entry name");
        }
        if (std::optional<OperationResult> error =
                validate_basename_only_name_field("Filesystem comment request", "entry_name", request.entry_name);
            error.has_value()) {
            return error;
        }
        return std::nullopt;
    }

} // namespace z7::app
