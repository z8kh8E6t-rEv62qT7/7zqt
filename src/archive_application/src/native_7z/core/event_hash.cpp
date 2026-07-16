// src/archive_application/src/native_7z/core/event_hash.cpp
// Role: Hash input collection and hash summary conversion helpers.

#include "core/internal.h"

namespace z7::app {
    namespace {

        inline constexpr size_t kHashDigestStringSize = k_HashCalc_DigestSize_Max * 2 + k_HashCalc_ExtraSize * 2 + 16;

        void append_scan_error(std::vector<HashScanError>& scan_errors,
                               fs::path const& path,
                               std::error_code const& error) {
            if (error) {
                scan_errors.push_back({path, error});
            }
        }

    } // namespace

    std::string path_leaf_name(fs::path const& path) {
        std::string name = path.filename().generic_string();
        if (!name.empty()) {
            return name;
        }
        return path.generic_string();
    }

    void collect_hash_entries_for_path(fs::path const& selected_path,
                                       std::string const& display_name,
                                       bool recursive_dirs,
                                       std::vector<HashInputEntry>& entries,
                                       std::vector<HashScanError>& scan_errors,
                                       uint64_t& total_files,
                                       uint64_t& total_bytes) {
        std::error_code status_error;
        fs::file_status const selected_link_status = fs::symlink_status(selected_path, status_error);
        if (status_error) {
            append_scan_error(scan_errors, selected_path, status_error);
            return;
        }
        fs::file_status const selected_status = fs::status(selected_path, status_error);
        if (status_error) {
            append_scan_error(scan_errors, selected_path, status_error);
            return;
        }

        bool const selected_is_dir = fs::is_directory(selected_status);
        if (selected_is_dir) {
            entries.push_back({selected_path, display_name, true, 0});
            if (!recursive_dirs || fs::is_symlink(selected_link_status)) {
                return;
            }

            struct DirectoryFrame {
                fs::path path;
                fs::directory_iterator current;
                fs::directory_iterator end;
            };

            auto open_directory = [&](fs::path const& path) -> std::optional<DirectoryFrame> {
                std::error_code iterator_error;
                fs::directory_iterator current(path, fs::directory_options::none, iterator_error);
                if (iterator_error) {
                    append_scan_error(scan_errors, path, iterator_error);
                    return std::nullopt;
                }
                return DirectoryFrame{path, std::move(current), fs::directory_iterator()};
            };

            std::vector<DirectoryFrame> stack;
            if (std::optional<DirectoryFrame> root = open_directory(selected_path); root.has_value()) {
                stack.push_back(std::move(*root));
            }

            while (!stack.empty()) {
                DirectoryFrame& frame = stack.back();
                if (frame.current == frame.end) {
                    stack.pop_back();
                    continue;
                }

                fs::directory_entry const child_entry = *frame.current;
                fs::path const child = child_entry.path();
                std::error_code increment_error;
                frame.current.increment(increment_error);
                if (increment_error) {
                    append_scan_error(scan_errors, frame.path, increment_error);
                    frame.current = frame.end;
                }

                std::error_code rel_ec;
                fs::path const rel = fs::relative(child, selected_path, rel_ec);
                std::string const rel_text = rel_ec ? child.filename().generic_string() : rel.generic_string();
                std::string const item_name = display_name + "/" + rel_text;

                std::error_code link_status_error;
                fs::file_status const link_status = child_entry.symlink_status(link_status_error);
                if (link_status_error) {
                    append_scan_error(scan_errors, child, link_status_error);
                    continue;
                }

                std::error_code child_status_error;
                fs::file_status const child_status = child_entry.status(child_status_error);
                if (child_status_error) {
                    append_scan_error(scan_errors, child, child_status_error);
                    continue;
                }

                if (fs::is_directory(child_status)) {
                    entries.push_back({child, item_name, true, 0});
                    if (!fs::is_symlink(link_status)) {
                        if (std::optional<DirectoryFrame> nested = open_directory(child); nested.has_value()) {
                            stack.push_back(std::move(*nested));
                        }
                    }
                } else if (fs::is_regular_file(child_status)) {
                    std::error_code size_error;
                    uint64_t const size = fs::file_size(child, size_error);
                    if (size_error) {
                        append_scan_error(scan_errors, child, size_error);
                        continue;
                    }
                    entries.push_back({child, item_name, false, size});
                    ++total_files;
                    total_bytes += size;
                }
            }
            return;
        }

        std::error_code size_error;
        uint64_t const size = fs::file_size(selected_path, size_error);
        if (size_error) {
            append_scan_error(scan_errors, selected_path, size_error);
            return;
        }
        entries.push_back({selected_path, display_name, false, size});
        ++total_files;
        total_bytes += size;
    }

    HashSummary make_hash_summary(CHashBundle const& bundle) {
        HashSummary summary;
        summary.num_dirs = bundle.NumDirs;
        summary.num_files = bundle.NumFiles;
        summary.num_alt_streams = bundle.NumAltStreams;
        summary.files_size = bundle.FilesSize;
        summary.alt_streams_size = bundle.AltStreamsSize;
        summary.num_errors = bundle.NumErrors;
        summary.main_name = ustring_to_utf8(bundle.MainName);
        summary.first_file_name = ustring_to_utf8(bundle.FirstFileName);

        char digest[kHashDigestStringSize];
        for (unsigned i = 0; i < bundle.Hashers.Size(); ++i) {
            CHasherState const& hasher = bundle.Hashers[i];
            HashMethodDigest method;
            method.method_name = astring_to_std(hasher.Name);

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_DataSum, digest);
            method.data_sum = digest;
            method.has_data_sum = !method.data_sum.empty();

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_NamesSum, digest);
            method.names_sum = digest;
            method.has_names_sum = !method.names_sum.empty();

            digest[0] = 0;
            hasher.WriteToString(k_HashCalc_Index_StreamsSum, digest);
            method.streams_sum = digest;
            method.has_streams_sum = !method.streams_sum.empty();

            summary.methods.push_back(std::move(method));
        }

        return summary;
    }

} // namespace z7::app
