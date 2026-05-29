// src/archive_application/src/native_7z/core/core_event_fs.cpp
// Role: Event emission and filesystem/recycle-bin helper routines.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

#include <cerrno>
#include <ctime>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace z7::app {

namespace {

enum class RecycleMetadataMode {
  kNone,
  kFreedesktop,
};

struct RecycleLocation {
  fs::path files_dir;
  fs::path info_dir;
  RecycleMetadataMode metadata_mode = RecycleMetadataMode::kNone;
};

#if !defined(_WIN32)
fs::path current_user_home_dir(std::error_code& ec) {
  ec.clear();
  errno = 0;
  const passwd* const entry = getpwuid(getuid());
  if (entry == nullptr || entry->pw_dir == nullptr || entry->pw_dir[0] == '\0') {
    ec = errno != 0
             ? std::error_code(errno, std::generic_category())
             : std::make_error_code(std::errc::no_such_file_or_directory);
    return {};
  }

  return fs::path(entry->pw_dir);
}
#endif

#if defined(_WIN32)
fs::path source_drive_recycle_root(const fs::path& source_path, std::error_code& ec) {
  ec.clear();
  fs::path absolute_path = source_path;
  if (!absolute_path.is_absolute()) {
    const fs::path cwd = fs::current_path(ec);
    if (ec) {
      return {};
    }
    absolute_path = cwd / source_path;
  }

  const fs::path root = absolute_path.root_path();
  if (root.empty()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return {};
  }

  return root / "$Recycle.Bin";
}
#endif

RecycleLocation recycle_location_for_path(const fs::path& source_path,
                                          std::error_code& ec) {
  ec.clear();

#if !defined(_WIN32)
  (void)source_path;
#endif

#if defined(_WIN32)
  return RecycleLocation{
      source_drive_recycle_root(source_path, ec),
      {},
      RecycleMetadataMode::kNone};
#elif defined(__APPLE__)
  const fs::path home = current_user_home_dir(ec);
  if (ec) {
    return {};
  }
  return RecycleLocation{home / ".Trash", {}, RecycleMetadataMode::kNone};
#else
  const fs::path home = current_user_home_dir(ec);
  if (ec) {
    return {};
  }
  const fs::path trash_root = home / ".local" / "share" / "Trash";
  return RecycleLocation{
      trash_root / "files",
      trash_root / "info",
      RecycleMetadataMode::kFreedesktop};
#endif
}

bool ensure_writable_directory(const fs::path& dir, std::error_code& ec) {
  ec.clear();
  if (dir.empty()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  if (!fs::exists(dir, ec)) {
    fs::create_directories(dir, ec);
  }
  if (ec) {
    return false;
  }

  const fs::path probe_path = dir /
                              fs::path(".z7-trash-probe-" +
                                       std::to_string(
                                           static_cast<long long>(
                                               std::chrono::steady_clock::now()
                                                   .time_since_epoch()
                                                   .count())));
  {
    std::ofstream probe(probe_path, std::ios::binary | std::ios::trunc);
    if (!probe.is_open()) {
      ec = std::make_error_code(std::errc::permission_denied);
      return false;
    }
  }

  fs::remove(probe_path, ec);
  return !ec;
}

bool ensure_recycle_location(const RecycleLocation& location, std::error_code& ec) {
  if (!ensure_writable_directory(location.files_dir, ec)) {
    return false;
  }

  if (location.metadata_mode == RecycleMetadataMode::kFreedesktop &&
      !ensure_writable_directory(location.info_dir, ec)) {
    return false;
  }

  ec.clear();
  return true;
}

fs::path freedesktop_info_path_for_target(const RecycleLocation& location,
                                          const fs::path& target) {
  fs::path info_name = target.filename();
  info_name += ".trashinfo";
  return location.info_dir / info_name;
}

bool path_exists(const fs::path& path, std::error_code& ec) {
  ec.clear();
  const bool exists = fs::exists(path, ec);
  if (ec) {
    return false;
  }
  return exists;
}

bool recycle_target_exists(const RecycleLocation& location,
                           const fs::path& target,
                           std::error_code& ec) {
  if (path_exists(target, ec)) {
    return true;
  }
  if (ec) {
    return false;
  }

  if (location.metadata_mode == RecycleMetadataMode::kFreedesktop &&
      path_exists(freedesktop_info_path_for_target(location, target), ec)) {
    return true;
  }
  return false;
}

#if !defined(_WIN32) && !defined(__APPLE__)
std::string freedesktop_percent_encode_path(const fs::path& path) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  const std::u8string raw = path.generic_u8string();
  std::string encoded;
  encoded.reserve(raw.size());

  for (const char8_t byte : raw) {
    const unsigned char value = static_cast<unsigned char>(byte);
    const bool unreserved =
        (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') ||
        value == '/' || value == '-' || value == '_' || value == '.' || value == '~';
    if (unreserved) {
      encoded.push_back(static_cast<char>(value));
      continue;
    }

    encoded.push_back('%');
    encoded.push_back(kHex[value >> 4]);
    encoded.push_back(kHex[value & 0x0F]);
  }

  return encoded;
}

std::string freedesktop_deletion_date() {
  std::time_t now = std::time(nullptr);
  std::tm local_time{};
  if (localtime_r(&now, &local_time) == nullptr) {
    return {};
  }

  char buffer[sizeof("YYYY-MM-DDTHH:MM:SS")] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_time) == 0) {
    return {};
  }
  return std::string(buffer);
}

bool write_freedesktop_trash_info(const RecycleLocation& location,
                                  const fs::path& original_path,
                                  const fs::path& target,
                                  std::error_code& ec) {
  ec.clear();

  fs::path absolute_original = original_path;
  if (!absolute_original.is_absolute()) {
    const fs::path cwd = fs::current_path(ec);
    if (ec) {
      return false;
    }
    absolute_original = cwd / original_path;
  }

  const std::string deletion_date = freedesktop_deletion_date();
  if (deletion_date.empty()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  const fs::path info_path = freedesktop_info_path_for_target(location, target);
  std::ofstream out(info_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    ec = std::make_error_code(std::errc::permission_denied);
    return false;
  }

  out << "[Trash Info]\n"
      << "Path=" << freedesktop_percent_encode_path(absolute_original) << '\n'
      << "DeletionDate=" << deletion_date << '\n';
  out.close();
  if (!out) {
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }

  return true;
}
#endif

bool write_recycle_metadata(const RecycleLocation& location,
                            const fs::path& original_path,
                            const fs::path& target,
                            std::error_code& ec) {
#if defined(_WIN32) || defined(__APPLE__)
  (void)original_path;
  (void)target;
#endif

  if (location.metadata_mode == RecycleMetadataMode::kNone) {
    ec.clear();
    return true;
  }

#if !defined(_WIN32) && !defined(__APPLE__)
  return write_freedesktop_trash_info(location, original_path, target, ec);
#else
  ec = std::make_error_code(std::errc::not_supported);
  return false;
#endif
}

void remove_recycle_metadata(const RecycleLocation& location,
                             const fs::path& target) {
  if (location.metadata_mode != RecycleMetadataMode::kFreedesktop) {
    return;
  }

  std::error_code ignored_ec;
  fs::remove(freedesktop_info_path_for_target(location, target), ignored_ec);
}

}  // namespace

void emit_log_event(const ArchiveBackendHooks& hooks,
                    OperationStage stage,
                    OutputChannel channel,
                    const std::string& message,
                    const std::optional<BenchmarkTypedSnapshot>& benchmark_snapshot,
                    const std::optional<BenchmarkTypedSummary>& benchmark_summary) {
  if (!hooks.on_event) {
    return;
  }

  OperationEvent event;
  event.kind = OperationEventKind::kLog;
  event.stage = stage;
  event.output_channel = channel;
  event.message = message;
  event.benchmark_snapshot = benchmark_snapshot;
  event.benchmark_summary = benchmark_summary;
  hooks.on_event(event);
}

void emit_progress_event(const ArchiveBackendHooks& hooks,
                         OperationStage stage,
                         int percent,
                         bool totals_known,
                         uint64_t total_bytes,
                         uint64_t completed_bytes,
                         uint64_t total_files,
                         uint64_t completed_files,
                         uint64_t error_count,
                         const std::string& current_path,
                         const std::string& message,
                         const std::optional<ProgressRatioInfo>& ratio_info,
                         const std::optional<BenchmarkTypedSnapshot>& benchmark_snapshot,
                         const std::optional<BenchmarkTypedSummary>& benchmark_summary) {
  if (!hooks.on_event) {
    return;
  }

  OperationEvent event;
  event.kind = OperationEventKind::kProgress;
  event.stage = stage;
  event.percent = percent;
  event.totals_known = totals_known;
  event.total_bytes = total_bytes;
  event.completed_bytes = completed_bytes;
  event.total_files = total_files;
  event.completed_files = completed_files;
  event.error_count = error_count;
  event.current_path = current_path;
  event.message = message;
  event.ratio_info = ratio_info;
  event.benchmark_snapshot = benchmark_snapshot;
  event.benchmark_summary = benchmark_summary;
  hooks.on_event(event);
}

bool ensure_parent_dir(const fs::path& path, std::error_code& ec) {
  ec.clear();
  const fs::path parent = path.parent_path();
  if (parent.empty()) {
    return true;
  }

  if (fs::exists(parent, ec)) {
    ec.clear();
    return true;
  }

  fs::create_directories(parent, ec);
  return !ec;
}

bool remove_path_any(const fs::path& path, std::error_code& ec) {
  ec.clear();
  if (!fs::exists(path, ec)) {
    ec.clear();
    return true;
  }

  fs::remove_all(path, ec);
  return !ec;
}

bool copy_path_any(const fs::path& src,
                   const fs::path& dst,
                   bool overwrite,
                   std::error_code& ec) {
  ec.clear();
  if (!fs::exists(src, ec)) {
    return false;
  }

  if (fs::exists(dst, ec)) {
    if (!overwrite) {
      ec = std::make_error_code(std::errc::file_exists);
      return false;
    }

    if (!remove_path_any(dst, ec)) {
      return false;
    }
  }

  if (!ensure_parent_dir(dst, ec)) {
    return false;
  }

  fs::copy_options options = fs::copy_options::recursive;
  if (overwrite) {
    options |= fs::copy_options::overwrite_existing;
  }

  fs::copy(src, dst, options, ec);
  return !ec;
}

fs::path unique_path_in_recycle_bin(const RecycleLocation& location,
                                    const fs::path& original_path,
                                    std::error_code& ec) {
  ec.clear();
  fs::path file_name = original_path.filename();
  if (file_name.empty()) {
    file_name = fs::path("item");
  }

  fs::path candidate = location.files_dir / file_name;
  if (!recycle_target_exists(location, candidate, ec)) {
    if (ec) {
      return {};
    }
    ec.clear();
    return candidate;
  }

  const fs::path stem = file_name.stem();
  const fs::path ext = file_name.extension();
  for (int i = 1; i < 100000; ++i) {
    candidate = location.files_dir /
                fs::path(stem.string() + " (" + std::to_string(i) + ")" + ext.string());
    if (!recycle_target_exists(location, candidate, ec)) {
      if (ec) {
        return {};
      }
      ec.clear();
      return candidate;
    }
  }

  ec = std::make_error_code(std::errc::file_exists);
  return {};
}

bool move_path_to_recycle_bin(const fs::path& path, std::error_code& ec) {
  ec.clear();
  if (!fs::exists(path, ec)) {
    ec.clear();
    return true;
  }

  const RecycleLocation location = recycle_location_for_path(path, ec);
  if (ec || !ensure_recycle_location(location, ec)) {
    return false;
  }

  const fs::path target = unique_path_in_recycle_bin(location, path, ec);
  if (ec) {
    return false;
  }

  if (!write_recycle_metadata(location, path, target, ec)) {
    return false;
  }

  fs::rename(path, target, ec);
  if (!ec) {
    return true;
  }

  if (ec != std::errc::cross_device_link) {
    remove_recycle_metadata(location, target);
    return false;
  }

  std::error_code copy_ec;
  if (!copy_path_any(path, target, false, copy_ec)) {
    remove_recycle_metadata(location, target);
    ec = copy_ec;
    return false;
  }

  std::error_code remove_ec;
  if (!remove_path_any(path, remove_ec)) {
    std::error_code ignored_ec;
    remove_path_any(target, ignored_ec);
    remove_recycle_metadata(location, target);
    ec = remove_ec;
    return false;
  }

  ec.clear();
  return true;
}

bool is_hresult_io(HRESULT hr) {
  return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
         hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

ArchiveError map_hresult_to_archive_error(int hr) {
  if (hr == S_FALSE) {
    return make_archive_error(ArchiveErrorDomain::kUnsupportedFormat,
                              "Archive format is unsupported",
                              2);
  }
  if (hr == E_ABORT) {
    return make_archive_error(ArchiveErrorDomain::kCanceled,
                              "Operation canceled",
                              255);
  }
  if (hr == E_INVALIDARG) {
    return make_archive_error(ArchiveErrorDomain::kInvalidArguments,
                              "Invalid request arguments",
                              7);
  }
  if (hr == E_OUTOFMEMORY) {
    return make_archive_error(ArchiveErrorDomain::kBackendUnavailable,
                              "Requested backend is unavailable",
                              8);
  }
  if (is_hresult_io(hr)) {
    return make_archive_error(ArchiveErrorDomain::kIo,
                              "I/O error",
                              2);
  }
  return make_archive_error(ArchiveErrorDomain::kUnknown,
                            "Unknown archive backend error",
                            2);
}


}  // namespace z7::app
