// src/archive_application/src/native_7z/core/core_event_fs.cpp
// Role: Event emission and filesystem/recycle-bin helper routines.

#include <cerrno>
#include <ctime>

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

#if defined(__APPLE__)
#include <copyfile.h>
#endif

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/stat.h>
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
            passwd const* const entry = getpwuid(getuid());
            if (entry == nullptr || entry->pw_dir == nullptr || entry->pw_dir[0] == '\0') {
                ec = errno != 0 ? std::error_code(errno, std::generic_category())
                                : std::make_error_code(std::errc::no_such_file_or_directory);
                return {};
            }

            return fs::path(entry->pw_dir);
        }
#endif

#if defined(_WIN32)
        fs::path source_drive_recycle_root(fs::path const& source_path, std::error_code& ec) {
            ec.clear();
            fs::path absolute_path = source_path;
            if (!absolute_path.is_absolute()) {
                fs::path const cwd = fs::current_path(ec);
                if (ec) {
                    return {};
                }
                absolute_path = cwd / source_path;
            }

            fs::path const root = absolute_path.root_path();
            if (root.empty()) {
                ec = std::make_error_code(std::errc::invalid_argument);
                return {};
            }

            return root / "$Recycle.Bin";
        }
#endif

        RecycleLocation recycle_location_for_path(fs::path const& source_path, std::error_code& ec) {
            ec.clear();

#if !defined(_WIN32)
            (void)source_path;
#endif

#if defined(_WIN32)
            return RecycleLocation{source_drive_recycle_root(source_path, ec), {}, RecycleMetadataMode::kNone};
#elif defined(__APPLE__)
            fs::path const home = current_user_home_dir(ec);
            if (ec) {
                return {};
            }
            return RecycleLocation{home / ".Trash", {}, RecycleMetadataMode::kNone};
#else
            fs::path const home = current_user_home_dir(ec);
            if (ec) {
                return {};
            }
            fs::path const trash_root = home / ".local" / "share" / "Trash";
            return RecycleLocation{trash_root / "files", trash_root / "info", RecycleMetadataMode::kFreedesktop};
#endif
        }

        bool ensure_writable_directory(fs::path const& dir, std::error_code& ec) {
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

            fs::path const probe_path = dir
                                      / fs::path(".z7-trash-probe-"
                                                 + std::to_string(static_cast<long long>(
                                                     std::chrono::steady_clock::now().time_since_epoch().count())));
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

        bool ensure_recycle_location(RecycleLocation const& location, std::error_code& ec) {
            if (!ensure_writable_directory(location.files_dir, ec)) {
                return false;
            }

            if (location.metadata_mode == RecycleMetadataMode::kFreedesktop
                && !ensure_writable_directory(location.info_dir, ec)) {
                return false;
            }

            ec.clear();
            return true;
        }

        fs::path freedesktop_info_path_for_target(RecycleLocation const& location, fs::path const& target) {
            fs::path info_name = target.filename();
            info_name += ".trashinfo";
            return location.info_dir / info_name;
        }

        bool path_exists(fs::path const& path, std::error_code& ec) {
            ec.clear();
            bool const exists = fs::exists(path, ec);
            if (ec) {
                return false;
            }
            return exists;
        }

        bool recycle_target_exists(RecycleLocation const& location, fs::path const& target, std::error_code& ec) {
            if (path_exists(target, ec)) {
                return true;
            }
            if (ec) {
                return false;
            }

            if (location.metadata_mode == RecycleMetadataMode::kFreedesktop
                && path_exists(freedesktop_info_path_for_target(location, target), ec)) {
                return true;
            }
            return false;
        }

#if !defined(_WIN32) && !defined(__APPLE__)
        std::string freedesktop_percent_encode_path(fs::path const& path) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::u8string const raw = path.generic_u8string();
            std::string encoded;
            encoded.reserve(raw.size());

            for (char8_t const byte : raw) {
                unsigned char const value = static_cast<unsigned char>(byte);
                bool const unreserved = (value >= 'A' && value <= 'Z')
                                     || (value >= 'a' && value <= 'z')
                                     || (value >= '0' && value <= '9')
                                     || value == '/'
                                     || value == '-'
                                     || value == '_'
                                     || value == '.'
                                     || value == '~';
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

        bool write_freedesktop_trash_info(RecycleLocation const& location,
                                          fs::path const& original_path,
                                          fs::path const& target,
                                          std::error_code& ec) {
            ec.clear();

            fs::path absolute_original = original_path;
            if (!absolute_original.is_absolute()) {
                fs::path const cwd = fs::current_path(ec);
                if (ec) {
                    return false;
                }
                absolute_original = cwd / original_path;
            }

            std::string const deletion_date = freedesktop_deletion_date();
            if (deletion_date.empty()) {
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            fs::path const info_path = freedesktop_info_path_for_target(location, target);
            std::ofstream out(info_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                ec = std::make_error_code(std::errc::permission_denied);
                return false;
            }

            out
                << "[Trash Info]\n"
                << "Path="
                << freedesktop_percent_encode_path(absolute_original)
                << '\n'
                << "DeletionDate="
                << deletion_date
                << '\n';
            out.close();
            if (!out) {
                ec = std::make_error_code(std::errc::io_error);
                return false;
            }

            return true;
        }
#endif

        bool write_recycle_metadata(RecycleLocation const& location,
                                    fs::path const& original_path,
                                    fs::path const& target,
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

        void remove_recycle_metadata(RecycleLocation const& location, fs::path const& target) {
            if (location.metadata_mode != RecycleMetadataMode::kFreedesktop) {
                return;
            }

            std::error_code ignored_ec;
            fs::remove(freedesktop_info_path_for_target(location, target), ignored_ec);
        }

    } // namespace

    void emit_log_event(ArchiveBackendHooks const& hooks,
                        OperationStage stage,
                        OutputChannel channel,
                        std::string const& message,
                        OperationMessageKind message_kind,
                        std::optional<BenchmarkTypedSnapshot> const& benchmark_snapshot,
                        std::optional<BenchmarkTypedSummary> const& benchmark_summary) {
        if (!hooks.on_event) {
            return;
        }

        OperationEvent event;
        event.kind = OperationEventKind::kLog;
        event.message_kind = message_kind;
        event.stage = stage;
        event.output_channel = channel;
        event.message = message;
        event.benchmark_snapshot = benchmark_snapshot;
        event.benchmark_summary = benchmark_summary;
        try {
            hooks.on_event(event);
        } catch (...) {
            // Delegate exceptions must never cross a COM callback boundary.
        }
    }

    void emit_progress_event(ArchiveBackendHooks const& hooks,
                             OperationStage stage,
                             int percent,
                             bool totals_known,
                             uint64_t total_bytes,
                             uint64_t completed_bytes,
                             uint64_t total_files,
                             uint64_t completed_files,
                             uint64_t error_count,
                             std::string const& current_path,
                             std::string const& message,
                             std::optional<ProgressRatioInfo> const& ratio_info,
                             std::optional<BenchmarkTypedSnapshot> const& benchmark_snapshot,
                             std::optional<BenchmarkTypedSummary> const& benchmark_summary) {
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
        try {
            hooks.on_event(event);
        } catch (...) {
            // Delegate exceptions must never cross a COM callback boundary.
        }
    }

    bool ensure_parent_dir(fs::path const& path, std::error_code& ec) {
        ec.clear();
        fs::path const parent = path.parent_path();
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

    bool create_private_directory(fs::path const& path, std::error_code& ec) {
        ec.clear();
#if defined(_WIN32)
        if (!fs::create_directory(path, ec)) {
            return false;
        }
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
        if (!ec) {
            return true;
        }
        std::error_code cleanup_ec;
        fs::remove(path, cleanup_ec);
        return false;
#else
        if (::mkdir(path.c_str(), S_IRWXU) == 0) {
            return true;
        }
        ec = std::error_code(errno, std::generic_category());
        return false;
#endif
    }

    bool remove_path_any(fs::path const& path, std::error_code& ec) {
        ec.clear();
        fs::file_status const status = fs::symlink_status(path, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
            return true;
        }
        if (ec) {
            return false;
        }
        if (!fs::status_known(status) || status.type() == fs::file_type::not_found) {
            return true;
        }

        fs::remove_all(path, ec);
        return !ec;
    }

    FilesystemObjectIdentity capture_filesystem_object_identity_no_follow(fs::path const& path, std::error_code& ec) {
        ec.clear();
        FilesystemObjectIdentity identity;
#if defined(_WIN32)
        HANDLE const handle = ::CreateFileW(path.c_str(),
                                            0,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr,
                                            OPEN_EXISTING,
                                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            return identity;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!::GetFileInformationByHandle(handle, &info)) {
            ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            ::CloseHandle(handle);
            return identity;
        }
        ::CloseHandle(handle);
        identity.volume = info.dwVolumeSerialNumber;
        identity.object = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
        struct stat info{};
        if (::lstat(path.c_str(), &info) != 0) {
            ec = std::error_code(errno, std::generic_category());
            return identity;
        }
        identity.volume = static_cast<uint64_t>(info.st_dev);
        identity.object = static_cast<uint64_t>(info.st_ino);
#endif
        identity.defined = true;
        return identity;
    }

    bool filesystem_object_matches_identity_no_follow(fs::path const& path,
                                                      FilesystemObjectIdentity const& identity,
                                                      std::error_code& ec) {
        ec.clear();
        if (!identity.defined) {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        FilesystemObjectIdentity const current = capture_filesystem_object_identity_no_follow(path, ec);
        return !ec && current.defined && current.volume == identity.volume && current.object == identity.object;
    }

    bool copy_path_any(fs::path const& src, fs::path const& dst, bool overwrite, std::error_code& ec) {
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

    bool copy_regular_file_with_metadata(fs::path const& src, fs::path const& dst, std::error_code& ec) {
        ec.clear();
#if defined(__APPLE__)
        if (::copyfile(src.c_str(), dst.c_str(), nullptr, COPYFILE_ALL | COPYFILE_EXCL) == 0) {
            return true;
        }
        ec = std::error_code(errno, std::generic_category());
        return false;
#elif defined(_WIN32)
        if (::CopyFileW(src.c_str(), dst.c_str(), TRUE)) {
            return true;
        }
        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
#else
        if (!fs::copy_file(src, dst, fs::copy_options::none, ec)) {
            return false;
        }
        fs::file_status const source_status = fs::status(src, ec);
        if (ec) {
            return false;
        }
        fs::permissions(dst, source_status.permissions(), fs::perm_options::replace, ec);
        if (ec) {
            return false;
        }
        fs::file_time_type const source_mtime = fs::last_write_time(src, ec);
        if (ec) {
            return false;
        }
        fs::last_write_time(dst, source_mtime, ec);
        if (ec) {
            return false;
        }
        return true;
#endif
    }

    bool copy_file_metadata(fs::path const& src, fs::path const& dst, std::error_code& ec) {
        ec.clear();
#if defined(__APPLE__)
        if (::copyfile(src.c_str(), dst.c_str(), nullptr, COPYFILE_METADATA) == 0) {
            return true;
        }
        ec = std::error_code(errno, std::generic_category());
        return false;
#else
        fs::file_status const source_status = fs::status(src, ec);
        if (ec) {
            return false;
        }
        fs::permissions(dst, source_status.permissions(), fs::perm_options::replace, ec);
        if (ec) {
            return false;
        }
        fs::file_time_type const source_mtime = fs::last_write_time(src, ec);
        if (ec) {
            return false;
        }
        fs::last_write_time(dst, source_mtime, ec);
        if (ec) {
            return false;
        }
        return true;
#endif
    }

    fs::path
    unique_path_in_recycle_bin(RecycleLocation const& location, fs::path const& original_path, std::error_code& ec) {
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

        fs::path const stem = file_name.stem();
        fs::path const ext = file_name.extension();
        for (int i = 1; i < 100000; ++i) {
            candidate = location.files_dir / fs::path(stem.string() + " (" + std::to_string(i) + ")" + ext.string());
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

    bool move_path_to_recycle_bin(fs::path const& path, std::error_code& ec) {
        ec.clear();
        if (!fs::exists(path, ec)) {
            ec.clear();
            return true;
        }

        RecycleLocation const location = recycle_location_for_path(path, ec);
        if (ec || !ensure_recycle_location(location, ec)) {
            return false;
        }

        fs::path const target = unique_path_in_recycle_bin(location, path, ec);
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
        return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
            || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)
            || hr == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)
            || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    ArchiveError map_hresult_to_archive_error(int hr) {
        if (hr == S_FALSE) {
            return make_archive_error(ArchiveErrorDomain::kUnsupportedFormat, "Archive format is unsupported", 2);
        }
        if (hr == E_ABORT) {
            return make_archive_error(ArchiveErrorDomain::kCanceled, "Operation canceled", 255);
        }
        if (hr == E_INVALIDARG) {
            return make_archive_error(ArchiveErrorDomain::kInvalidArguments, "Invalid request arguments", 7);
        }
        if (hr == E_OUTOFMEMORY) {
            return make_archive_error(ArchiveErrorDomain::kBackendUnavailable, "Requested backend is unavailable", 8);
        }
        if (is_hresult_io(hr)) {
            return make_archive_error(ArchiveErrorDomain::kIo, "I/O error", 2);
        }
        return make_archive_error(ArchiveErrorDomain::kUnknown, "Unknown archive backend error", 2);
    }

} // namespace z7::app
