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

    std::string NativeExtractCallback::report_path_without_following_leaf(fs::path const& path) {
        std::error_code absolute_ec;
        fs::path const absolute_path = fs::absolute(path, absolute_ec);
        if (absolute_ec) {
            return path.lexically_normal().generic_string();
        }

        fs::path const leaf = absolute_path.filename();
        fs::path const parent = leaf.empty() ? absolute_path : absolute_path.parent_path();
        std::error_code canonical_ec;
        fs::path const canonical_parent = fs::weakly_canonical(parent, canonical_ec);
        fs::path reported = canonical_ec ? parent.lexically_normal() : canonical_parent;
        if (!leaf.empty()) {
            reported /= leaf;
        }
        return reported.lexically_normal().generic_string();
    }

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

            try {
                return hooks_.ask_overwrite(prompt);
            } catch (...) {
                record_io_error("Overwrite callback failed for: " + destination_path.generic_string());
                return OverwriteDecision::kCancel;
            }
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
        bool const original_is_absolute = is_absolute_item_path(item_path);
        std::string original_item_path = item_path;
        std::replace(original_item_path.begin(), original_item_path.end(), '\\', fs::path::preferred_separator);
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
        auto finalize_path = [this](ResolvedPath value) {
            std::pair<fs::path, fs::path> const* best = nullptr;
            size_t best_depth = 0;
            for (auto const& remap : directory_destination_remaps_) {
                fs::path const relative = value.destination_path.lexically_relative(remap.first);
                if (relative.empty() && value.destination_path != remap.first) {
                    continue;
                }
                auto const first = relative.begin();
                if (first != relative.end() && *first == "..") {
                    continue;
                }
                size_t const depth = static_cast<size_t>(std::distance(remap.first.begin(), remap.first.end()));
                if (best == nullptr || depth > best_depth) {
                    best = &remap;
                    best_depth = depth;
                }
            }
            if (best != nullptr) {
                fs::path const relative = value.destination_path.lexically_relative(best->first);
                value.destination_path = best->second / relative;
                fs::path const authorized_relative = value.authorized_root.lexically_relative(best->first);
                auto const authorized_first = authorized_relative.begin();
                bool const authorized_root_is_remapped =
                    value.authorized_root == best->first
                    || (!authorized_relative.empty()
                        && !(authorized_first != authorized_relative.end() && *authorized_first == ".."));
                if (authorized_root_is_remapped) {
                    value.authorized_root = best->second / authorized_relative;
                }
            }
            value.absolute_output_path = report_path_without_following_leaf(value.destination_path);
            return value;
        };

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
            return finalize_path(std::move(out));
        }

        if (normalized_item.empty()) {
            out.destination_path = output_dir_;
            out.authorized_root = output_dir_;
            out.absolute_output_path = fs::absolute(output_dir_).generic_string();
            return finalize_path(std::move(out));
        }

        if (path_mode_ == ExtractPathMode::kNoPaths) {
            normalized_item = base_name_for_no_paths(normalized_item);
        }

        if (path_mode_ == ExtractPathMode::kAbsolutePaths && original_is_absolute) {
            out.destination_path = fs::path(original_item_path).lexically_normal();
            out.absolute_output_path = fs::absolute(out.destination_path).generic_string();
            return finalize_path(std::move(out));
        }

        fs::path destination = output_dir_;
        destination /= fs::path(normalized_item);
        out.destination_path = destination;
        out.authorized_root = output_dir_;
        out.absolute_output_path = fs::absolute(destination).generic_string();
        return finalize_path(std::move(out));
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
        fs::path native_path(path);
#ifdef _WIN32
        return native_path.has_root_name() && native_path.has_root_directory();
#else
        return native_path.is_absolute();
#endif
    }

    bool NativeExtractCallback::validate_output_item_path(std::string const& path, std::string& reason) {
        reason.clear();
#ifdef _WIN32
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        size_t start = 0;
        if (normalized.size() >= 3 && normalized[1] == ':' && normalized[2] == '/') {
            start = 3;
        } else {
            while (start < normalized.size() && normalized[start] == '/') {
                ++start;
            }
        }

        auto is_reserved = [](std::string component) {
            size_t const dot = component.find('.');
            if (dot != std::string::npos) {
                component.resize(dot);
            }
            std::transform(component.begin(), component.end(), component.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            static std::unordered_set<std::string> const reserved{"CON",
                                                                  "PRN",
                                                                  "AUX",
                                                                  "NUL",
                                                                  "CLOCK$",
                                                                  "CONIN$",
                                                                  "CONOUT$",
                                                                  "COM1",
                                                                  "COM2",
                                                                  "COM3",
                                                                  "COM4",
                                                                  "COM5",
                                                                  "COM6",
                                                                  "COM7",
                                                                  "COM8",
                                                                  "COM9",
                                                                  "LPT1",
                                                                  "LPT2",
                                                                  "LPT3",
                                                                  "LPT4",
                                                                  "LPT5",
                                                                  "LPT6",
                                                                  "LPT7",
                                                                  "LPT8",
                                                                  "LPT9",
                                                                  "COM\xC2\xB9",
                                                                  "COM\xC2\xB2",
                                                                  "COM\xC2\xB3",
                                                                  "LPT\xC2\xB9",
                                                                  "LPT\xC2\xB2",
                                                                  "LPT\xC2\xB3"};
            return reserved.find(component) != reserved.end();
        };

        while (start <= normalized.size()) {
            size_t const end = normalized.find('/', start);
            std::string const component =
                normalized.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!component.empty()) {
                if (component.find(':') != std::string::npos) {
                    reason = "Windows alternate data stream syntax is not allowed";
                    return false;
                }
                if (component.back() == '.' || component.back() == ' ') {
                    reason = "Windows path components cannot end in a dot or space";
                    return false;
                }
                if (is_reserved(component)) {
                    reason = "Windows reserved device name is not allowed";
                    return false;
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
#else
        (void)path;
#endif
        return true;
    }

    std::string NativeExtractCallback::base_name_for_no_paths(std::string const& path) {
        size_t const slash = path.find_last_of("/\\");
        if (slash == std::string::npos) {
            return path;
        }
        return path.substr(slash + 1);
    }

} // namespace z7::app
