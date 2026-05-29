#include "core/filesystem_replace.h"

#include "core/internal_results.h"

namespace z7::app {
namespace {

const AtomicReplaceFileOps& default_atomic_replace_file_ops() {
  static const AtomicReplaceFileOps kOps = {
      .exists = [](const fs::path& path, std::error_code& ec) {
        return fs::exists(path, ec);
      },
      .rename = [](const fs::path& from,
                   const fs::path& to,
                   std::error_code& ec) { fs::rename(from, to, ec); },
      .remove = [](const fs::path& path, std::error_code& ec) {
        fs::remove(path, ec);
      },
      .make_unique_sibling_path = nullptr,
  };
  return kOps;
}

const AtomicReplaceFileOps& resolve_atomic_replace_file_ops(
    const AtomicReplaceFileOps* ops) {
  return ops != nullptr ? *ops : default_atomic_replace_file_ops();
}

AtomicReplaceOptions resolve_atomic_replace_options(
    const AtomicReplaceOptions* options) {
  return options != nullptr ? *options : AtomicReplaceOptions{};
}

bool file_exists_with_ops(const fs::path& path,
                          std::error_code& ec,
                          const AtomicReplaceFileOps& ops) {
  if (ops.exists) {
    return ops.exists(path, ec);
  }
  return fs::exists(path, ec);
}

void rename_with_ops(const fs::path& from,
                     const fs::path& to,
                     std::error_code& ec,
                     const AtomicReplaceFileOps& ops) {
  if (ops.rename) {
    ops.rename(from, to, ec);
    return;
  }
  fs::rename(from, to, ec);
}

void remove_with_ops(const fs::path& path,
                     std::error_code& ec,
                     const AtomicReplaceFileOps& ops) {
  if (ops.remove) {
    ops.remove(path, ec);
    return;
  }
  fs::remove(path, ec);
}

std::optional<OperationResult> make_replace_io_failure(std::string message) {
  return make_operation_failure<OperationResult>(
      ArchiveErrorDomain::kIo, std::move(message), 2);
}

std::string preserved_temp_suffix(const fs::path& source_path) {
  return " temporary file preserved at " + source_path.string();
}

}  // namespace

fs::path make_unique_sibling_path(const fs::path& path,
                                  std::string_view suffix,
                                  std::error_code& ec,
                                  const AtomicReplaceFileOps* ops) {
  ec.clear();
  const AtomicReplaceFileOps& file_ops = resolve_atomic_replace_file_ops(ops);
  if (file_ops.make_unique_sibling_path) {
    return file_ops.make_unique_sibling_path(path, suffix);
  }

  const fs::path base = path.has_parent_path() ? path.parent_path() : fs::current_path(ec);
  if (ec || base.empty()) {
    return {};
  }

  const std::string file_name = path.filename().string();
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int i = 0; i < 64; ++i) {
    const fs::path candidate =
        base / fs::path(file_name + std::string(suffix) +
                        std::to_string(static_cast<long long>(ticks + i)));
    std::error_code exists_ec;
    if (!file_exists_with_ops(candidate, exists_ec, file_ops)) {
      if (exists_ec) {
        ec = exists_ec;
        return {};
      }
      return candidate;
    }
    if (exists_ec) {
      ec = exists_ec;
      return {};
    }
  }

  ec = std::make_error_code(std::errc::file_exists);
  return {};
}

AtomicReplaceResult replace_file_atomically(const fs::path& source_path,
                                            const fs::path& destination_path,
                                            std::string_view backup_suffix,
                                            const AtomicReplaceFileOps* ops,
                                            const AtomicReplaceOptions* options) {
  const AtomicReplaceFileOps& file_ops = resolve_atomic_replace_file_ops(ops);
  const AtomicReplaceOptions replace_options =
      resolve_atomic_replace_options(options);
  AtomicReplaceResult result;
  result.source_path = source_path;
  result.destination_path = destination_path;

  std::error_code ec;
  result.destination_existed = file_exists_with_ops(destination_path, ec, file_ops);
  if (ec) {
    result.error = make_replace_io_failure(ec.message());
    result.source_exists = file_exists_with_ops(source_path, ec, file_ops);
    return result;
  }

  if (!result.destination_existed) {
    rename_with_ops(source_path, destination_path, ec, file_ops);
    result.source_exists = file_exists_with_ops(source_path, ec, file_ops);
    if (!ec) {
      result.success = true;
      return result;
    }

    std::string message =
        "Failed to replace archive file: " + ec.message();
    if (result.source_exists) {
      message += preserved_temp_suffix(source_path);
    }
    result.error = make_replace_io_failure(std::move(message));
    return result;
  }

  std::error_code backup_path_ec;
  result.backup_path =
      make_unique_sibling_path(destination_path, backup_suffix, backup_path_ec, &file_ops);
  if (result.backup_path.empty()) {
    result.source_exists = file_exists_with_ops(source_path, ec, file_ops);
    std::string message = "Failed to allocate backup path for archive replacement";
    if (backup_path_ec) {
      message += ": " + backup_path_ec.message();
    }
    result.error = make_replace_io_failure(
        std::move(message));
    return result;
  }

  rename_with_ops(destination_path, result.backup_path, ec, file_ops);
  if (ec) {
    result.source_exists = file_exists_with_ops(source_path, ec, file_ops);
    result.error = make_replace_io_failure(ec.message());
    return result;
  }

  std::error_code promote_ec;
  rename_with_ops(source_path, destination_path, promote_ec, file_ops);
  result.source_exists = file_exists_with_ops(source_path, ec, file_ops);
  if (!promote_ec) {
    if (replace_options.preserve_backup_on_success) {
      result.backup_retained = true;
    } else {
      std::error_code cleanup_ec;
      remove_with_ops(result.backup_path, cleanup_ec, file_ops);
      result.backup_path.clear();
    }
    result.success = true;
    result.source_exists = false;
    return result;
  }

  std::error_code restore_ec;
  rename_with_ops(result.backup_path, destination_path, restore_ec, file_ops);
  result.original_restored = !restore_ec;
  result.original_restore_failed = static_cast<bool>(restore_ec);

  std::string message =
      "Failed to replace archive file: " + promote_ec.message();
  if (result.original_restore_failed) {
    message += "; original archive restore also failed: " + restore_ec.message();
  }
  if (result.source_exists) {
    message += preserved_temp_suffix(source_path);
  }
  result.error = make_replace_io_failure(std::move(message));
  return result;
}

}  // namespace z7::app
