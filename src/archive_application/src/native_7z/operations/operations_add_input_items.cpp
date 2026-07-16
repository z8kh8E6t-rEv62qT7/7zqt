// src/archive_application/src/native_7z/operations/operations_add_input_items.cpp
// Role: Add request input_items validation and direct physical-to-archive mapping.

#include "operations/operations_add_input_items.h"

namespace z7::app {
    std::optional<OperationResult> prepare_add_request_for_execution(AddRequest const& request,
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
        for (AddInputItem const& item : request.input_items) {
            std::string const normalized_entry = normalize_archive_virtual_directory(item.archive_entry);
            if (normalized_entry.empty()) {
                return invalid_request("Add request input_items archive_entry must not be empty");
            }
            if (!archive_virtual_path_is_safe_for_materialization(normalized_entry)) {
                return invalid_request("Add request input_items archive_entry contains unsafe path segment: "
                                       + normalized_entry);
            }
            if (!seen_entries.insert(normalized_entry).second) {
                return invalid_request("Add request input_items contain duplicate archive_entry: " + normalized_entry);
            }

            prepared.input_items.push_back(AddInputItem{item.filesystem_path, normalized_entry});
            prepared.input_paths.push_back(item.filesystem_path);
        }

        *out_request = std::move(prepared);
        return std::nullopt;
    }

} // namespace z7::app
