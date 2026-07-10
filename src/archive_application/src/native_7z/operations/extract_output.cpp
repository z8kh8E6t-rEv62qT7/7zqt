// src/archive_application/src/native_7z/operations/extract_output.cpp
// Role: Archive extract output path helpers.

#include "operations/extract_output.h"

#include <filesystem>
#include <unordered_set>

#include "common/archive_type_normalization.h"
#include "common/ascii_text.h"
#include "common/path_text_normalization.h"

namespace z7::app {
    namespace {

        bool is_extract_archive_ext(std::string const& ext) {
            static std::unordered_set<std::string> const kExts{"7z", "rar", "zip"};
            if (z7::common::is_gzip_family_archive_suffix(ext) || z7::common::is_bzip2_family_archive_suffix(ext)) {
                return true;
            }
            return kExts.find(z7::common::to_lower_ascii_copy(ext)) != kExts.end();
        }

        std::string suggested_extract_subfolder_name(std::string const& archive_name) {
            size_t const dot_pos = archive_name.rfind('.');
            if (dot_pos == std::string::npos) {
                return archive_name.empty() ? std::string("Archive~") : archive_name + "~";
            }

            std::string const ext = z7::common::to_lower_ascii_copy(archive_name.substr(dot_pos + 1));
            std::string base = z7::common::trim_ascii_space_copy(archive_name.substr(0, dot_pos));

            size_t const dot2 = base.rfind('.');
            if (dot2 != std::string::npos && dot2 > 0) {
                std::string const ext2 = z7::common::to_lower_ascii_copy(base.substr(dot2 + 1));
                bool const remove_prev = (ext == "001" && is_extract_archive_ext(ext2))
                                      || (ext == "rar" && (ext2 == "part001" || ext2 == "part01" || ext2 == "part1"));
                if (remove_prev) {
                    base = z7::common::trim_ascii_space_copy(base.substr(0, dot2));
                }
            }

            if (base.empty()) {
                return archive_name.empty() ? std::string("Archive") : archive_name;
            }
            return base;
        }

    } // namespace

    std::string resolve_multi_archive_output_dir(std::string const& output_template, std::string const& archive_path) {
        if (output_template.find('*') == std::string::npos) {
            return output_template;
        }

        std::string name = std::filesystem::path(archive_path).filename().string();
        if (name.empty()) {
            name = archive_path;
        }

        std::string const subdir = suggested_extract_subfolder_name(name);
        std::string resolved = output_template;
        size_t pos = 0;
        while ((pos = resolved.find('*', pos)) != std::string::npos) {
            resolved.replace(pos, 1, subdir);
            pos += subdir.size();
        }
        return resolved;
    }

    std::string output_tail_name(std::string output_dir) {
        output_dir = z7::common::normalize_native_separators_copy(std::move(output_dir));
        while (!output_dir.empty() && output_dir.back() == '/') {
            output_dir.pop_back();
        }
        if (output_dir.empty()) {
            return {};
        }

        size_t const slash = output_dir.find_last_of('/');
        if (slash == std::string::npos) {
            return output_dir;
        }
        return output_dir.substr(slash + 1);
    }

} // namespace z7::app
