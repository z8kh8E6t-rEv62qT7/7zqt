#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include "archive_types.h"

namespace z7::app {

    namespace fs = std::filesystem;

    struct AtomicReplaceFileOps {
        std::function<bool(fs::path const&, std::error_code&)> exists;
        std::function<void(fs::path const&, fs::path const&, std::error_code&)> rename;
        std::function<void(fs::path const&, std::error_code&)> remove;
        std::function<fs::path(fs::path const&, std::string_view)> make_unique_sibling_path;
    };

    struct AtomicReplaceOptions {
        bool preserve_backup_on_success = false;
    };

    struct AtomicReplaceResult {
        bool success = false;
        bool destination_existed = false;
        bool original_restored = false;
        bool original_restore_failed = false;
        bool backup_retained = false;
        bool source_exists = false;
        fs::path source_path;
        fs::path destination_path;
        fs::path backup_path;
        std::optional<OperationResult> error;
    };

    fs::path make_unique_sibling_path(fs::path const& path,
                                      std::string_view suffix,
                                      std::error_code& ec,
                                      AtomicReplaceFileOps const* ops = nullptr);

    AtomicReplaceResult replace_file_atomically(fs::path const& source_path,
                                                fs::path const& destination_path,
                                                std::string_view backup_suffix,
                                                AtomicReplaceFileOps const* ops = nullptr,
                                                AtomicReplaceOptions const* options = nullptr);

} // namespace z7::app
