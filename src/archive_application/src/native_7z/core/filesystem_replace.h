#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include "archive_types.h"

namespace z7::app {

namespace fs = std::filesystem;

struct AtomicReplaceFileOps {
  std::function<bool(const fs::path&, std::error_code&)> exists;
  std::function<void(const fs::path&, const fs::path&, std::error_code&)> rename;
  std::function<void(const fs::path&, std::error_code&)> remove;
  std::function<fs::path(const fs::path&, std::string_view)> make_unique_sibling_path;
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

fs::path make_unique_sibling_path(const fs::path& path,
                                  std::string_view suffix,
                                  std::error_code& ec,
                                  const AtomicReplaceFileOps* ops = nullptr);

AtomicReplaceResult replace_file_atomically(const fs::path& source_path,
                                            const fs::path& destination_path,
                                            std::string_view backup_suffix,
                                            const AtomicReplaceFileOps* ops = nullptr,
                                            const AtomicReplaceOptions* options = nullptr);

}  // namespace z7::app
