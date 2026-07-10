// src/archive_application/src/native_7z/callbacks/callbacks_extract_paths.cpp
// Role: Extract callback path and overwrite helper methods.

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {
    namespace {

        std::optional<int64_t> filesystem_time_msecs_utc(fs::path const& path) {
            std::error_code ec;
            fs::file_time_type const file_time = fs::last_write_time(path, ec);
            if (ec) {
                return std::nullopt;
            }
            auto const system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            return static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count());
        }

        bool path_has_prefix(std::string const& path, std::string const& prefix) {
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

        std::string strip_prefix_with_separator(std::string const& path, std::string const& prefix) {
            if (prefix.empty()) {
                return path;
            }
            if (path == prefix) {
                return {};
            }
            if (!path_has_prefix(path, prefix)) {
                return path;
            }
            return path.substr(prefix.size() + 1);
        }

        int remap_match_specificity(ExtractPathRemap const& remap) {
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

        bool path_components_start_with(fs::path const& path, fs::path const& prefix) {
            auto path_it = path.begin();
            for (auto prefix_it = prefix.begin(); prefix_it != prefix.end(); ++prefix_it, ++path_it) {
                if (path_it == path.end() || *path_it != *prefix_it) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    bool NativeExtractCallback::path_is_within_authorized_root(fs::path const& candidate,
                                                               fs::path const& authorized_root,
                                                               std::error_code& ec) const {
        ec.clear();
        if (authorized_root.empty()) {
            return true;
        }

        fs::path const absolute_root = fs::absolute(authorized_root, ec);
        if (ec) {
            return false;
        }
        fs::path const resolved_root = fs::weakly_canonical(absolute_root, ec);
        if (ec) {
            return false;
        }

        fs::path const absolute_candidate = fs::absolute(candidate, ec);
        if (ec) {
            return false;
        }
        fs::path const resolved_candidate = fs::weakly_canonical(absolute_candidate, ec);
        if (ec) {
            return false;
        }

        return path_components_start_with(resolved_candidate, resolved_root);
    }

    OverwriteDecision NativeExtractCallback::ask_overwrite_decision(fs::path const& destination_path,
                                                                    UInt32 index,
                                                                    std::string const& item_path) {
        if (hooks_.ask_overwrite) {
            OverwritePrompt prompt;
            prompt.existing_path = destination_path.generic_string();
            prompt.incoming_path = item_path;

            std::error_code ec;
            uint64_t const existing_size = fs::file_size(destination_path, ec);
            if (!ec) {
                prompt.existing_size_defined = true;
                prompt.existing_size = existing_size;
            }
            prompt.existing_mtime_msecs_utc = filesystem_time_msecs_utc(destination_path);

            uint64_t incoming_size = 0;
            if (archive_get_prop_uint64(archive_, index, kpidSize, incoming_size)) {
                prompt.incoming_size_defined = true;
                prompt.incoming_size = incoming_size;
            }
            int64_t incoming_mtime = 0;
            if (archive_get_prop_time_msecs_utc(archive_, index, kpidMTime, incoming_mtime)) {
                prompt.incoming_mtime_msecs_utc = incoming_mtime;
            }

            return hooks_.ask_overwrite(prompt);
        }

        if (!ask_mode_notice_emitted_) {
            ask_mode_notice_emitted_ = true;
            emit_log_event(hooks_,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Overwrite mode 'Ask' is not interactive in this path; existing files are skipped.");
        }
        return OverwriteDecision::kNo;
    }

    NativeExtractCallback::ResolvedPath NativeExtractCallback::resolve_destination_path(std::string const& item_path,
                                                                                        bool is_directory) const {
        std::string const normalized_archive_item = normalize_archive_item_path(item_path);
        std::string normalized_item = normalized_archive_item;
        if (!eliminate_prefix_.empty()) {
            if (normalized_item == eliminate_prefix_) {
                normalized_item.clear();
            } else if (normalized_item.size() > eliminate_prefix_.size()
                       && normalized_item.compare(0, eliminate_prefix_.size(), eliminate_prefix_) == 0
                       && normalized_item[eliminate_prefix_.size()] == '/') {
                normalized_item.erase(0, eliminate_prefix_.size() + 1);
            }
        }
        ResolvedPath out;

        ExtractPathRemap const* best_remap = nullptr;
        int best_specificity = -1;
        for (ExtractPathRemap const& remap : path_remaps_) {
            bool matches = false;
            switch (remap.match_kind) {
                case ExtractPathRemapMatchKind::kRequestRoot:
                    matches = request_selects_single_logical_root();
                    break;
                case ExtractPathRemapMatchKind::kExactArchivePath:
                    matches = normalized_archive_item == remap.source_path;
                    break;
                case ExtractPathRemapMatchKind::kArchivePrefix:
                    matches = path_has_prefix(normalized_archive_item, remap.source_path);
                    break;
            }
            if (!matches) {
                continue;
            }
            int const specificity = remap_match_specificity(remap);
            if (specificity > best_specificity) {
                best_remap = &remap;
                best_specificity = specificity;
                continue;
            }
            if (specificity == best_specificity) {
                best_remap = nullptr;
            }
        }

        if (best_remap != nullptr) {
            fs::path destination = fs::path(best_remap->destination_path);
            std::string relative_tail;
            switch (best_remap->match_kind) {
                case ExtractPathRemapMatchKind::kRequestRoot:
                    if (!selected_entries_.empty()) {
                        relative_tail = strip_prefix_with_separator(normalized_archive_item, selected_entries_.front());
                    } else {
                        relative_tail = normalized_archive_item;
                    }
                    break;
                case ExtractPathRemapMatchKind::kExactArchivePath:
                    relative_tail.clear();
                    break;
                case ExtractPathRemapMatchKind::kArchivePrefix:
                    relative_tail = strip_prefix_with_separator(normalized_archive_item, best_remap->source_path);
                    break;
            }
            bool const maps_entry_to_exact_file = !is_directory && relative_tail.empty();
            out.authorized_root =
                best_remap->match_kind == ExtractPathRemapMatchKind::kExactArchivePath || maps_entry_to_exact_file
                    ? destination.parent_path()
                    : destination;
            if (!relative_tail.empty()) {
                destination /= fs::path(relative_tail);
            }
            out.destination_path = destination;
            out.absolute_output_path = fs::absolute(destination).generic_string();
            return out;
        }

        if (normalized_item.empty()) {
            out.destination_path = output_dir_;
            out.authorized_root = output_dir_;
            out.absolute_output_path = fs::absolute(output_dir_).generic_string();
            return out;
        }

        if (path_mode_ == ExtractPathMode::kNoPaths) {
            normalized_item = base_name_for_no_paths(normalized_item);
        }

        if (path_mode_ == ExtractPathMode::kAbsolutePaths && is_absolute_item_path(normalized_item)) {
            out.destination_path = fs::path(normalized_item);
            out.absolute_output_path = fs::absolute(out.destination_path).generic_string();
            return out;
        }

        fs::path destination = output_dir_;
        destination /= fs::path(normalized_item);
        out.destination_path = destination;
        out.authorized_root = output_dir_;
        out.absolute_output_path = fs::absolute(destination).generic_string();
        return out;
    }

    std::string NativeExtractCallback::normalize_path_for_output(std::string item_path) const {
        item_path = normalize_archive_item_path(item_path);
        if (!eliminate_prefix_.empty()) {
            if (item_path == eliminate_prefix_) {
                item_path.clear();
            } else if (item_path.size() > eliminate_prefix_.size()
                       && item_path.compare(0, eliminate_prefix_.size(), eliminate_prefix_) == 0
                       && item_path[eliminate_prefix_.size()] == '/') {
                item_path.erase(0, eliminate_prefix_.size() + 1);
            }
        }
        return item_path;
    }

    bool NativeExtractCallback::is_absolute_item_path(std::string const& path) {
        if (path.empty()) {
            return false;
        }
        if (path[0] == '/' || path[0] == '\\') {
            return true;
        }
        if (path.size() > 1 && path[1] == ':') {
            return true;
        }
        return false;
    }

    std::string NativeExtractCallback::base_name_for_no_paths(std::string const& path) {
        size_t const slash = path.find_last_of("/\\");
        if (slash == std::string::npos) {
            return path;
        }
        return path.substr(slash + 1);
    }

} // namespace z7::app
