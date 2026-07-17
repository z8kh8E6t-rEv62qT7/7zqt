// src/archive_application/src/native_7z/session/session_registry.cpp
// Role: Nested-archive preview session storage and lifecycle.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

#include "common/archive_type_normalization.h"
#include "core/filesystem_replace.h"
#include "core/internal.h"
#include "session/session_parent_item_replace.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_extract_run.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace z7::app {

    FilesystemObjectVersion capture_filesystem_object_version(std::filesystem::path const& path,
                                                               std::error_code& ec) {
        FilesystemObjectVersion version;
        ec.clear();
        version.identity = capture_filesystem_object_identity_no_follow(path, ec);
        if (ec || !version.identity.defined) {
            return version;
        }
#if defined(_WIN32)
        HANDLE const handle = ::CreateFileW(path.c_str(),
                                            FILE_READ_ATTRIBUTES,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr,
                                            OPEN_EXISTING,
                                            FILE_FLAG_OPEN_REPARSE_POINT,
                                            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            return version;
        }
        FILE_STANDARD_INFO standard_info{};
        FILE_BASIC_INFO basic_info{};
        bool const ok = ::GetFileInformationByHandleEx(
                            handle, FileStandardInfo, &standard_info, sizeof(standard_info))
                     && ::GetFileInformationByHandleEx(handle, FileBasicInfo, &basic_info, sizeof(basic_info));
        int const error = ok ? ERROR_SUCCESS : static_cast<int>(::GetLastError());
        ::CloseHandle(handle);
        if (!ok) {
            ec = std::error_code(error, std::system_category());
            return version;
        }
        version.size = static_cast<uint64_t>(standard_info.EndOfFile.QuadPart);
        version.mtime_ticks = basic_info.LastWriteTime.QuadPart;
        version.change_ticks = basic_info.ChangeTime.QuadPart;
#else
        struct stat info {};
        if (::lstat(path.c_str(), &info) != 0) {
            ec = std::error_code(errno, std::generic_category());
            return version;
        }
        version.size = static_cast<uint64_t>(info.st_size);
#if defined(__APPLE__)
        version.mtime_ticks = static_cast<int64_t>(info.st_mtimespec.tv_sec) * 1000000000LL
                            + info.st_mtimespec.tv_nsec;
        version.change_ticks = static_cast<int64_t>(info.st_ctimespec.tv_sec) * 1000000000LL
                             + info.st_ctimespec.tv_nsec;
#else
        version.mtime_ticks = static_cast<int64_t>(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec;
        version.change_ticks = static_cast<int64_t>(info.st_ctim.tv_sec) * 1000000000LL + info.st_ctim.tv_nsec;
#endif
#endif
        version.defined = true;
        return version;
    }

    bool filesystem_object_version_matches(FilesystemObjectVersion const& lhs,
                                           FilesystemObjectVersion const& rhs) {
        return lhs.defined && rhs.defined && lhs.identity.defined && rhs.identity.defined
            && lhs.identity.volume == rhs.identity.volume && lhs.identity.object == rhs.identity.object
            && lhs.size == rhs.size && lhs.mtime_ticks == rhs.mtime_ticks
            && lhs.change_ticks == rhs.change_ticks;
    }

    namespace {

        constexpr std::string_view kSessionRootBackupSuffix = ".z7-session-backup-";

        std::optional<OperationResult> commit_archive_session_to_root(ArchiveOpenSession& session);

        void remove_path_tree(std::filesystem::path const& path) {
            if (path.empty()) {
                return;
            }
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        std::filesystem::path make_temp_session_dir(std::filesystem::path preferred_base = {}) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path base = std::move(preferred_base);
            if (base.empty()) {
                base = fs::temp_directory_path(ec);
            }
            if (ec || base.empty()) {
                base = fs::path(".");
            }
            auto const ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            fs::path candidate =
                base / (std::string("z7-session-write-") + std::to_string(static_cast<long long>(ticks)));
            for (int i = 0; i < 32; ++i) {
                std::error_code dir_ec;
                if (create_private_directory(candidate, dir_ec)) {
                    return candidate;
                }
                candidate =
                    base / (std::string("z7-session-write-") + std::to_string(static_cast<long long>(ticks + i + 1)));
            }
            return {};
        }

        std::string normalize_session_format_token(std::string value) {
            value = z7::common::canonical_archive_type_token_copy(value);
            if (value == "*" || value == "#") {
                return {};
            }
            return value;
        }

        std::string archive_session_format_token(ArchiveOpenSession const& session) {
            ArchiveOpenSessionState const& state = archive_session_state(session);
            if (state.archive_link == nullptr || state.codecs == nullptr) {
                return {};
            }
            CArc const* arc = state.archive_link->GetArc();
            if (arc == nullptr
                || arc->FormatIndex < 0
                || static_cast<unsigned>(arc->FormatIndex) >= state.codecs->Formats.Size()) {
                return {};
            }
            wchar_t const* format_name = state.codecs->GetFormatNamePtr(static_cast<unsigned>(arc->FormatIndex));
            if (format_name == nullptr || format_name[0] == 0) {
                return {};
            }
            return normalize_session_format_token(ustring_to_utf8(UString(format_name)));
        }

        std::string archive_session_reopen_format_hint(ArchiveOpenSession const& session) {
            ArchiveOpenSessionState const& state = archive_session_state(session);
            CArc const* const arc = state.archive_link == nullptr ? nullptr : state.archive_link->GetArc();
            // Preserve parser-owned prefixes (for example SFX stubs) instead of forcing the inner format.
            if (arc != nullptr && arc->ArcStreamOffset != 0) {
                return {};
            }
            return archive_session_format_token(session);
        }

        std::filesystem::path session_materialized_file_path(ArchiveOpenSession const& session,
                                                             std::filesystem::path const& directory) {
            namespace fs = std::filesystem;
            ArchiveOpenSessionState const& state = archive_session_state(session);
            if (state.temp_file != nullptr && !state.temp_file->empty()) {
                return directory / state.temp_file->filename();
            }
            std::string const& entry_path = ArchiveOpenSessionNativeAccess::entry_path_from_parent(session);
            if (!entry_path.empty()) {
                fs::path const entry(entry_path);
                if (!entry.filename().empty()) {
                    return directory / entry.filename();
                }
            }
            std::string const& source_path = ArchiveOpenSessionNativeAccess::source_archive_path(session);
            if (!source_path.empty()) {
                fs::path const source(source_path);
                if (!source.filename().empty()) {
                    return directory / source.filename();
                }
            }
            fs::path const display(session.display_path());
            if (!display.filename().empty()) {
                return directory / display.filename();
            }
            return directory / fs::path("session.bin");
        }

        std::optional<OperationResult> write_buffer_to_file(std::vector<uint8_t> const& buffer,
                                                            std::filesystem::path const& path) {
            if (path.empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Writable session materialization path is empty", 2);
            }
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Failed to create writable session file: " + path.string(), 2);
            }
            if (!buffer.empty()) {
                out.write(reinterpret_cast<char const*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                if (!out.good()) {
                    return make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kIo, "Failed to write writable session file: " + path.string(), 2);
                }
            }
            return std::nullopt;
        }

        std::optional<OperationResult> materialize_session_backing_file(ArchiveOpenSession& session,
                                                                        ArchiveBackendHooks const& hooks,
                                                                        std::atomic<bool>* cancel_requested,
                                                                        std::function<bool()> wait_while_paused,
                                                                        std::filesystem::path* out_file_path,
                                                                        std::filesystem::path* out_dir_path) {
            namespace fs = std::filesystem;
            if (out_file_path == nullptr || out_dir_path == nullptr) {
                return invalid_request("Writable session materialization requires output paths");
            }

            ArchiveOpenSessionState& state = archive_session_state(session);
            if (state.temp_file != nullptr && !state.temp_file->empty()) {
                *out_file_path = *state.temp_file;
                *out_dir_path = state.temp_dir;
                return std::nullopt;
            }

            std::string const& source_path = ArchiveOpenSessionNativeAccess::source_archive_path(session);
            fs::path const preferred_base = source_path.empty() ? fs::path{} : fs::path(source_path).parent_path();
            fs::path dir = make_temp_session_dir(preferred_base);
            if (dir.empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Failed to create writable session temp directory", 2);
            }
            fs::path const file_path = session_materialized_file_path(session, dir);

            if (state.memory_buffer.empty()) {
                if (!source_path.empty()) {
                    std::error_code copy_ec;
                    if (!copy_regular_file_with_metadata(fs::path(source_path), file_path, copy_ec)) {
                        remove_path_tree(dir);
                        return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                                       "Failed to materialize writable root archive: "
                                                                           + copy_ec.message(),
                                                                       2);
                    }
                } else if (ArchiveOpenSessionNativeAccess::parent(session) != nullptr) {
                    auto const& parent = ArchiveOpenSessionNativeAccess::parent(session);
                    CArchiveLink& parent_link = archive_session_link(*parent);
                    CArc const* parent_arc = parent_link.GetArc();
                    if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
                        remove_path_tree(dir);
                        return make_operation_failure<OperationResult>(
                            ArchiveErrorDomain::kIo,
                            "Parent archive not available for writable nested session materialization",
                            2);
                    }
                    UInt32 resolved_index = 0;
                    if (std::optional<OperationResult> validation_error =
                            validate_archive_session_parent_item(session, *parent_arc, &resolved_index);
                        validation_error.has_value()) {
                        remove_path_tree(dir);
                        return validation_error;
                    }

                    ArchiveBackendHooks const parent_hooks = make_session_password_hooks(*parent, hooks);
                    auto* callback =
                        new NativeExtractCallback(parent_arc,
                                                  dir,
                                                  parent_hooks,
                                                  cancel_requested,
                                                  std::move(wait_while_paused),
                                                  parent->display_path(),
                                                  {},
                                                  OverwriteMode::kOverwrite,
                                                  ExtractPathMode::kNoPaths,
                                                  std::string{},
                                                  {},
                                                  parent->password_defined() ? parent->password() : std::string{},
                                                  ExtractZoneIdMode::kNone,
                                                  false,
                                                  1);
                    UInt32 const indices[1] = {resolved_index};
                    const HRESULT extract_hr = parent_arc->Archive->Extract(indices, 1, /*testMode=*/0, callback);
                    callback->Release();
                    if (extract_hr != S_OK) {
                        remove_path_tree(dir);
                        if (extract_hr == E_ABORT) {
                            return make_operation_canceled<OperationResult>();
                        }
                        return make_operation_failure_from_hresult<OperationResult>(extract_hr);
                    }
                    if (!fs::exists(file_path)) {
                        fs::path extracted_fallback;
                        std::error_code it_ec;
                        for (auto const& entry : fs::directory_iterator(dir, it_ec)) {
                            if (entry.is_regular_file()) {
                                extracted_fallback = entry.path();
                                break;
                            }
                        }
                        if (extracted_fallback.empty()) {
                            remove_path_tree(dir);
                            return make_operation_failure<OperationResult>(
                                ArchiveErrorDomain::kIo,
                                "Writable nested archive extraction did not produce a file",
                                2);
                        }
                        *out_file_path = extracted_fallback;
                        *out_dir_path = dir;
                        return std::nullopt;
                    }
                } else {
                    remove_path_tree(dir);
                    return make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kIo, "Session has no writable materialization source", 2);
                }
            } else {
                if (std::optional<OperationResult> write_error = write_buffer_to_file(state.memory_buffer, file_path);
                    write_error.has_value()) {
                    remove_path_tree(dir);
                    return write_error;
                }
            }

            *out_file_path = file_path;
            *out_dir_path = dir;
            return std::nullopt;
        }

        std::optional<OperationResult> reopen_archive_session_from_path(ArchiveOpenSession& session,
                                                                        std::filesystem::path const& file_path,
                                                                        std::filesystem::path const& dir_path,
                                                                        ArchiveBackendHooks const& hooks,
                                                                        std::atomic<bool>* cancel_requested,
                                                                        std::function<bool()> wait_while_paused) {
            if (file_path.empty()) {
                return invalid_request("Session reopen requires a materialized archive file");
            }

            std::string const format_hint = archive_session_reopen_format_hint(session);
            auto next_state = std::make_unique<ArchiveOpenSessionState>();
            next_state->source_version = archive_session_state(session).source_version;
            next_state->temp_dir = dir_path;
            next_state->temp_file = std::make_unique<std::filesystem::path>(file_path);
            next_state->archive_link = std::make_unique<CArchiveLink>();
            next_state->types = std::make_unique<CObjectVector<COpenType>>();
            next_state->excluded_formats = std::make_unique<CIntVector>();
            next_state->codecs = std::make_unique<CCodecs>();

            ArchiveBackendHooks const reopen_hooks = make_session_password_hooks(session, hooks);
            CArc const* arc = nullptr;
            bool password_requested = false;
            bool wrong_password = false;
            std::string password;
            const HRESULT hr = open_archive_shared(file_path.string(),
                                                   format_hint,
                                                   reopen_hooks,
                                                   cancel_requested,
                                                   std::move(wait_while_paused),
                                                   OpenResultMessagePolicy::kOperationMessages,
                                                   /*allow_password_prompt=*/true,
                                                   session.password_defined() ? session.password() : std::string(),
                                                   /*codecs_already_loaded=*/false,
                                                   *next_state->codecs,
                                                   *next_state->types,
                                                   *next_state->excluded_formats,
                                                   *next_state->archive_link,
                                                   arc,
                                                   &password_requested,
                                                   &wrong_password,
                                                   &password,
                                                   &next_state->open_diagnostics,
                                                   session.filename_code_page());
            if (hr != S_OK) {
                if (password_requested || wrong_password) {
                    return make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
                }
                if (hr == E_ABORT) {
                    return make_operation_canceled<OperationResult>();
                }
                return make_operation_failure_from_hresult<OperationResult>(hr);
            }

            if (auto const& parent = ArchiveOpenSessionNativeAccess::parent(session); parent != nullptr) {
                OpenArchiveDiagnostics inherited = archive_session_state(*parent).open_diagnostics;
                append_open_archive_diagnostics(inherited, next_state->open_diagnostics);
                next_state->open_diagnostics = std::move(inherited);
            }
            if (next_state->open_diagnostics.has_errors()) {
                return make_operation_failure_from_open_diagnostics<OperationResult>(
                    next_state->open_diagnostics);
            }

            if (!password.empty()) {
                session.set_password(std::move(password));
            }
            ArchiveOpenSessionNativeAccess::replace_state(session, std::move(next_state));
            ArchiveOpenSessionNativeAccess::set_strategy(session, OpenArchiveSessionResult::Strategy::kTempFile);
            return std::nullopt;
        }

        std::optional<OperationResult> commit_archive_session_to_parent(ArchiveOpenSession& session,
                                                                        ArchiveBackendHooks const& hooks,
                                                                        std::atomic<bool>* cancel_requested,
                                                                        std::function<bool()> wait_while_paused) {
            auto const& parent = ArchiveOpenSessionNativeAccess::parent(session);
            if (parent == nullptr) {
                return invalid_request("Parent session commit requires a parent session");
            }
            ScopedFilenameCodePage parent_filename_scope(parent->filename_code_page());

            if (ArchiveOpenSessionNativeAccess::parent_generation_at_open(session)
                != ArchiveOpenSessionNativeAccess::generation(*parent)) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo,
                    "Nested archive changed after this session was opened; refusing to overwrite a newer version",
                    2);
            }

            ArchiveOpenSessionState& state = archive_session_state(session);
            if (state.temp_file == nullptr || state.temp_file->empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Dirty nested session has no writable archive file", 2);
            }

            if (std::optional<OperationResult> validation_error =
                    validate_archive_session_parent_item_replacement(*parent);
                validation_error.has_value()) {
                return validation_error;
            }

            if (std::optional<OperationResult> writable_error =
                    ensure_archive_session_writable(*parent, hooks, cancel_requested, wait_while_paused);
                writable_error.has_value()) {
                return writable_error;
            }
            ArchiveOpenSessionState const& parent_state = archive_session_state(*parent);
            if (parent_state.temp_file == nullptr || parent_state.temp_file->empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Parent session has no writable backing file", 2);
            }
            CArc const* parent_arc = archive_session_link(*parent).GetArc();
            if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Parent archive is unavailable for nested session commit", 2);
            }
            UInt32 parent_entry_index = 0;
            if (std::optional<OperationResult> validation_error =
                    validate_archive_session_parent_item(session, *parent_arc, &parent_entry_index);
                validation_error.has_value()) {
                return validation_error;
            }

            SessionMutationBackup parent_backup;
            if (std::optional<OperationResult> backup_error =
                    create_archive_session_mutation_backup(*parent, &parent_backup);
                backup_error.has_value()) {
                return backup_error;
            }
            bool const parent_was_dirty = ArchiveOpenSessionNativeAccess::dirty(*parent);
            uint64_t const parent_generation = ArchiveOpenSessionNativeAccess::generation(*parent);
            auto restore_parent = [&]() -> std::optional<OperationResult> {
                std::optional<OperationResult> restore_error = restore_archive_session_mutation_backup(
                    *parent, parent_backup, hooks, nullptr, []() { return true; });
                if (!restore_error.has_value()) {
                    ArchiveOpenSessionNativeAccess::set_dirty(*parent, parent_was_dirty);
                    ArchiveOpenSessionNativeAccess::set_generation(*parent, parent_generation);
                }
                return restore_error;
            };

            OperationResult const replace_result = replace_archive_session_item_by_index(
                *parent,
                parent_entry_index,
                *state.temp_file,
                hooks,
                cancel_requested,
                wait_while_paused);
            if (!replace_result.ok) {
                if (std::optional<OperationResult> restore_error = restore_parent(); restore_error.has_value()) {
                    return restore_error;
                }
                return replace_result;
            }

            if (std::optional<OperationResult> refresh_error =
                    refresh_archive_session_from_backing_file(*parent, hooks, cancel_requested, wait_while_paused);
                refresh_error.has_value()) {
                if (std::optional<OperationResult> restore_error = restore_parent(); restore_error.has_value()) {
                    return restore_error;
                }
                return refresh_error;
            }

            ArchiveOpenSessionNativeAccess::set_dirty(*parent, true);
            ArchiveOpenSessionNativeAccess::increment_generation(*parent);

            auto const registered_parent = ArchiveSessionRegistry::instance().find(parent->token());
            if (registered_parent.get() != parent.get()) {
                std::optional<OperationResult> propagation_error;
                if (ArchiveOpenSessionNativeAccess::parent(*parent) != nullptr) {
                    propagation_error =
                        commit_archive_session_to_parent(*parent, hooks, cancel_requested, wait_while_paused);
                } else {
                    propagation_error = commit_archive_session_to_root(*parent);
                }
                if (propagation_error.has_value()) {
                    if (std::optional<OperationResult> restore_error = restore_parent(); restore_error.has_value()) {
                        return restore_error;
                    }
                    return propagation_error;
                }
            }

            ArchiveOpenSessionNativeAccess::set_dirty(session, false);
            if (std::optional<OperationResult> cleanup_error = discard_archive_session_mutation_backup(parent_backup);
                cleanup_error.has_value()) {
                emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kStdErr, cleanup_error->error.message);
            }
            return std::nullopt;
        }

        std::optional<OperationResult> commit_archive_session_to_root(ArchiveOpenSession& session) {
            ArchiveOpenSessionState& state = archive_session_state(session);
            if (state.temp_file == nullptr || state.temp_file->empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Dirty root session has no writable archive file", 2);
            }
            std::string const& source_path = ArchiveOpenSessionNativeAccess::source_archive_path(session);
            if (source_path.empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Root session is missing its source archive path", 7);
            }
            if (!state.source_version.has_value() || !state.source_version->defined) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Root session is missing its source version", 2);
            }

            std::error_code metadata_ec;
            if (!copy_file_metadata(std::filesystem::path(source_path), *state.temp_file, metadata_ec)) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo,
                    "Failed to preserve root archive metadata before commit: " + metadata_ec.message(),
                    2);
            }

            FilesystemObjectVersion const expected_version = *state.source_version;
            std::error_code current_version_ec;
            FilesystemObjectVersion const current_version = capture_filesystem_object_version(
                std::filesystem::path(source_path), current_version_ec);
            if (current_version_ec || !filesystem_object_version_matches(expected_version, current_version)) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo,
                    "stale archive source: source version changed before commit"
                        + (current_version_ec ? std::string(": ") + current_version_ec.message() : ""),
                    2);
            }
            AtomicReplaceOptions replace_options;
            replace_options.expected_destination_identity = &expected_version.identity;
            replace_options.validate_quarantined_destination =
                [expected_version](std::filesystem::path const& quarantined, std::string& diagnostic) {
                    std::error_code version_ec;
                    FilesystemObjectVersion const actual = capture_filesystem_object_version(quarantined, version_ec);
                    // Moving into the transaction can update change-time on
                    // some filesystems. Identity, size and mtime remain stable
                    // and are checked after the atomic move; change-time was
                    // checked immediately before it above.
                    if (!version_ec && actual.defined && actual.identity.defined
                        && actual.identity.volume == expected_version.identity.volume
                        && actual.identity.object == expected_version.identity.object
                        && actual.size == expected_version.size
                        && actual.mtime_ticks == expected_version.mtime_ticks) {
                        return true;
                    }
                    diagnostic = "source version changed before commit";
                    if (version_ec) {
                        diagnostic += ": " + version_ec.message();
                    }
                    return false;
                };
            AtomicReplaceResult const replace_result = replace_file_atomically(
                *state.temp_file,
                std::filesystem::path(source_path),
                kSessionRootBackupSuffix,
                nullptr,
                &replace_options);
            if (!replace_result.success) {
                if (replace_result.replacement_committed) {
                    ArchiveOpenSessionNativeAccess::set_dirty(session, false);
                    state.temp_file.reset();
                    remove_path_tree(state.temp_dir);
                    state.temp_dir.clear();
                }
                if (replace_result.error.has_value()) {
                    return replace_result.error;
                }
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Failed to commit writable root archive session", 2);
            }

            ArchiveOpenSessionNativeAccess::set_dirty(session, false);
            state.temp_file.reset();
            remove_path_tree(state.temp_dir);
            state.temp_dir.clear();
            return std::nullopt;
        }

    } // namespace

    std::string archive_item_path_for_matching(CArc const& arc, UInt32 index) {
        UString path;
        if (arc.GetItem_Path(index, path) == S_OK) {
            return normalize_archive_item_path(ustring_to_utf8(path));
        }
        if (arc.Archive == nullptr) {
            return {};
        }
        return normalize_archive_item_path(archive_get_prop_text(arc.Archive, index, kpidPath));
    }

    std::optional<OperationResult> validate_archive_session_parent_item(ArchiveOpenSession const& session,
                                                                        CArc const& parent_arc,
                                                                        UInt32* out_index) {
        if (out_index == nullptr) {
            return invalid_request("Nested session parent-item validation requires an output index");
        }
        std::optional<uint32_t> const stored_index =
            ArchiveOpenSessionNativeAccess::parent_entry_index(session);
        if (!stored_index.has_value()) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Nested session is missing its parent entry index", 7);
        }
        if (parent_arc.Archive == nullptr) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Parent archive is unavailable for nested session validation", 2);
        }

        UInt32 num_items = 0;
        HRESULT const count_hr = parent_arc.Archive->GetNumberOfItems(&num_items);
        if (count_hr != S_OK) {
            return make_operation_failure_from_hresult<OperationResult>(count_hr);
        }
        UInt32 const index = static_cast<UInt32>(*stored_index);
        if (index >= num_items) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Nested archive parent entry index is no longer valid", 2);
        }

        std::string const expected = normalize_archive_item_path(
            ArchiveOpenSessionNativeAccess::entry_path_from_parent(session));
        std::string const actual = archive_item_path_for_matching(parent_arc, index);
        if (actual != expected) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Nested archive parent entry changed at index " + std::to_string(index),
                2);
        }
        *out_index = index;
        return std::nullopt;
    }

    ArchiveBackendHooks make_session_password_hooks(ArchiveOpenSession& session,
                                                    ArchiveBackendHooks const& base_hooks) {
        ArchiveBackendHooks hooks = base_hooks;
        hooks.ask_password = [&session, base = base_hooks.ask_password](PasswordPrompt const& prompt) {
            if (prompt.reason_kind == PasswordPromptReason::kWrongPassword) {
                session.clear_password();
            }
            if (session.password_defined()) {
                PasswordReply reply;
                reply.kind = PasswordReplyKind::kProvide;
                reply.password = session.password();
                return reply;
            }
            if (base) {
                PasswordReply const reply = base(prompt);
                if (reply.kind == PasswordReplyKind::kProvide) {
                    session.set_password(reply.password);
                }
                return reply;
            }
            return PasswordReply{};
        };
        return hooks;
    }

    // ---------------------------------------------------------------------------
    // ArchiveOpenSession

    ArchiveOpenSession::ArchiveOpenSession() : state_(std::make_unique<ArchiveOpenSessionState>()) {}

    ArchiveOpenSession::~ArchiveOpenSession() {
        if (state_ == nullptr) {
            return;
        }
        ArchiveOpenSessionState& state = *state_;
        // Destruction order matters: CArchiveLink references CCodecs, so release the
        // link first. Temp files are removed eagerly while the path state is still
        // available.
        state.archive_link.reset();
        state.excluded_formats.reset();
        state.types.reset();
        state.codecs.reset();
        state.stream_ref_holder.reset();
        if (!state.temp_dir.empty()) {
            remove_path_tree(state.temp_dir);
        }
    }

    void ArchiveOpenSession::set_password(std::string value) {
        password_ = std::move(value);
        password_defined_ = true;
    }

    void ArchiveOpenSession::clear_password() {
        password_.clear();
        password_defined_ = false;
    }

    size_t ArchiveOpenSession::depth() const {
        size_t depth_value = 0;
        std::shared_ptr<ArchiveOpenSession> current = parent_;
        while (current) {
            ++depth_value;
            current = current->parent_;
        }
        return depth_value;
    }

    // ---------------------------------------------------------------------------
    // ArchiveSessionRegistry

    ArchiveSessionRegistry& ArchiveSessionRegistry::instance() {
        static ArchiveSessionRegistry registry;
        return registry;
    }

    ArchiveSessionToken ArchiveSessionRegistry::allocate_token() {
        ArchiveSessionToken token;
        token.value = next_token_.fetch_add(1, std::memory_order_relaxed);
        return token;
    }

    std::shared_ptr<ArchiveOpenSession>
    ArchiveSessionRegistry::register_session(std::shared_ptr<ArchiveOpenSession> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session->token_.value] = session;
        return session;
    }

    bool ArchiveSessionRegistry::close(ArchiveSessionToken token) {
        OperationResult const result = close_native_archive_session(
            *this, token, NestedDirtyClosePolicy::kCommit, {}, nullptr, [] { return true; });
        return result.ok;
    }

    std::shared_ptr<ArchiveOpenSession> ArchiveSessionRegistry::find(ArchiveSessionToken token) const {
        if (!token.is_valid()) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = sessions_.find(token.value);
        if (it == sessions_.end()) {
            return nullptr;
        }
        return it->second;
    }

    size_t ArchiveSessionRegistry::session_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

    bool ArchiveSessionRegistry::has_descendant(ArchiveSessionToken token) const {
        if (!token.is_valid()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto const& [candidate_token, candidate] : sessions_) {
            if (candidate_token == token.value || candidate == nullptr) {
                continue;
            }
            for (std::shared_ptr<ArchiveOpenSession> cursor = ArchiveOpenSessionNativeAccess::parent(*candidate);
                 cursor != nullptr;
                 cursor = ArchiveOpenSessionNativeAccess::parent(*cursor)) {
                if (cursor->token() == token) {
                    return true;
                }
            }
        }
        return false;
    }

    std::optional<OperationResult> ensure_archive_session_writable(ArchiveOpenSession& session,
                                                                   ArchiveBackendHooks const& hooks,
                                                                   std::atomic<bool>* cancel_requested,
                                                                   std::function<bool()> wait_while_paused) {
        ArchiveOpenSessionState& state = archive_session_state(session);
        if (state.open_diagnostics.has_errors()) {
            return make_operation_failure_from_open_diagnostics<OperationResult>(state.open_diagnostics);
        }
        if (state.temp_file != nullptr && !state.temp_file->empty()) {
            return std::nullopt;
        }

        std::filesystem::path file_path;
        std::filesystem::path dir_path;
        if (std::optional<OperationResult> materialize_error = materialize_session_backing_file(
                session, hooks, cancel_requested, wait_while_paused, &file_path, &dir_path);
            materialize_error.has_value()) {
            return materialize_error;
        }

        std::optional<OperationResult> reopen_error = reopen_archive_session_from_path(
            session, file_path, dir_path, hooks, cancel_requested, std::move(wait_while_paused));
        if (reopen_error.has_value()) {
            remove_path_tree(dir_path);
        }
        return reopen_error;
    }

    std::optional<OperationResult> refresh_archive_session_from_backing_file(ArchiveOpenSession& session,
                                                                             ArchiveBackendHooks const& hooks,
                                                                             std::atomic<bool>* cancel_requested,
                                                                             std::function<bool()> wait_while_paused) {
        ArchiveOpenSessionState const& state = archive_session_state(session);
        if (state.temp_file == nullptr || state.temp_file->empty()) {
            return invalid_request("Writable session refresh requires a backing file");
        }
        return reopen_archive_session_from_path(
            session, *state.temp_file, state.temp_dir, hooks, cancel_requested, std::move(wait_while_paused));
    }

    std::optional<OperationResult> create_archive_session_mutation_backup(ArchiveOpenSession const& session,
                                                                          SessionMutationBackup* backup) {
        if (backup == nullptr) {
            return invalid_request("Session mutation backup requires an output path");
        }
        ArchiveOpenSessionState const& state = archive_session_state(session);
        if (state.temp_file == nullptr || state.temp_file->empty()) {
            return invalid_request("Session mutation backup requires a writable backing file");
        }
        std::error_code ec;
        std::unique_ptr<FilesystemTransaction> transaction =
            FilesystemTransaction::create(*state.temp_file, "session-mutation", ec);
        if (!transaction) {
            return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo,
                                                           "Failed to create private session mutation transaction"
                                                               + (ec ? std::string(": ") + ec.message() : ""),
                                                           2);
        }
        backup->transaction = std::shared_ptr<FilesystemTransaction>(std::move(transaction));
        backup->path = backup->transaction->allocate_path("backup");
        if (!copy_regular_file_with_metadata(*state.temp_file, backup->path, ec)) {
            std::error_code cleanup_ec;
            std::filesystem::remove(backup->path, cleanup_ec);
            backup->path.clear();
            backup->transaction.reset();
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Failed to create session mutation backup: " + ec.message(), 2);
        }
        backup->identity = capture_filesystem_object_identity_no_follow(backup->path, ec);
        if (ec || !backup->identity.defined) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Failed to identify session mutation backup: " + ec.message(), 2);
        }
        return std::nullopt;
    }

    std::optional<OperationResult> restore_archive_session_mutation_backup(ArchiveOpenSession& session,
                                                                           SessionMutationBackup const& backup,
                                                                           ArchiveBackendHooks const& hooks,
                                                                           std::atomic<bool>* cancel_requested,
                                                                           std::function<bool()> wait_while_paused) {
        ArchiveOpenSessionState const& state = archive_session_state(session);
        if (state.temp_file == nullptr || state.temp_file->empty() || backup.empty() || backup.transaction == nullptr) {
            return invalid_request("Session mutation restore requires backing and backup files");
        }
        std::filesystem::path const backing_path = *state.temp_file;
        std::error_code ec;
        if (!filesystem_object_matches_identity_no_follow(backup.path, backup.identity, ec)) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Session mutation backup is missing or changed; current backing file was preserved"
                    + (ec ? std::string(": ") + ec.message() : ""),
                2);
        }

        FilesystemObjectIdentity const failed_identity =
            capture_filesystem_object_identity_no_follow(backing_path, ec);
        if (ec || !failed_identity.defined) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Failed to identify current session backing file: " + ec.message(), 2);
        }
        TransactionMoveResult const quarantined = backup.transaction->quarantine(backing_path, &failed_identity);
        if (!quarantined.success) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Failed to preserve current session backing file: " + quarantined.diagnostic, 2);
        }
        TransactionMoveResult const restored = backup.transaction->restore(backup.path, backing_path);
        if (!restored.success) {
            TransactionMoveResult const failed_restore =
                backup.transaction->restore(quarantined.preserved_path, backing_path);
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Failed to restore session mutation backup: " + restored.diagnostic
                    + (failed_restore.success ? "; current backing file restored"
                                              : "; " + failed_restore.diagnostic),
                2);
        }
        TransactionMoveResult const discarded_failed =
            backup.transaction->discard(quarantined.preserved_path, &failed_identity);
        if (!discarded_failed.success) {
            return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo, discarded_failed.diagnostic, 2);
        }
        return refresh_archive_session_from_backing_file(
            session, hooks, cancel_requested, std::move(wait_while_paused));
    }

    std::optional<OperationResult> discard_archive_session_mutation_backup(SessionMutationBackup const& backup) {
        if (backup.empty()) {
            return std::nullopt;
        }
        if (backup.transaction == nullptr) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Session mutation backup has no owning transaction", 2);
        }
        TransactionMoveResult const discarded = backup.transaction->discard(backup.path, &backup.identity);
        if (!discarded.success) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Failed to remove session mutation backup: " + discarded.diagnostic,
                2);
        }
        std::string cleanup_diagnostic;
        if (!backup.transaction->finish(&cleanup_diagnostic)) {
            return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo, cleanup_diagnostic, 2);
        }
        return std::nullopt;
    }

    OperationResult close_native_archive_session(ArchiveSessionRegistry& registry,
                                                 ArchiveSessionToken token,
                                                 NestedDirtyClosePolicy nested_dirty_policy,
                                                 ArchiveBackendHooks const& hooks,
                                                 std::atomic<bool>* cancel_requested,
                                                 std::function<bool()> wait_while_paused) {
        if (!token.is_valid()) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }

        std::shared_ptr<ArchiveOpenSession> session = registry.find(token);
        if (!session) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }

        std::vector<std::shared_ptr<ArchiveOpenSession>> lock_chain;
        for (std::shared_ptr<ArchiveOpenSession> cursor = session; cursor != nullptr;
             cursor = ArchiveOpenSessionNativeAccess::parent(*cursor)) {
            lock_chain.push_back(cursor);
        }
        std::reverse(lock_chain.begin(), lock_chain.end());
        std::vector<std::unique_lock<std::recursive_mutex>> session_locks;
        session_locks.reserve(lock_chain.size());
        for (std::shared_ptr<ArchiveOpenSession> const& locked_session : lock_chain) {
            session_locks.emplace_back(ArchiveOpenSessionNativeAccess::operation_mutex(*locked_session));
        }

        if (ArchiveOpenSessionNativeAccess::dirty(*session)) {
            if (ArchiveOpenSessionNativeAccess::parent(*session) != nullptr) {
                auto const& parent = ArchiveOpenSessionNativeAccess::parent(*session);
                if (nested_dirty_policy == NestedDirtyClosePolicy::kPrompt) {
                    ChoicePrompt prompt;
                    prompt.kind = ChoicePromptKind::kUpdateModifiedNestedArchive;
                    prompt.subject_path = ArchiveOpenSessionNativeAccess::entry_path_from_parent(*session);
                    prompt.title = "7-Zip";
                    prompt.message = "Update modified nested archive?";
                    prompt.choices = {"commit", "discard", "cancel"};
                    prompt.default_index = 0;
                    ChoiceReply const reply = hooks.ask_choice ? hooks.ask_choice(prompt) : ChoiceReply{};
                    if (reply.kind != ChoiceReplyKind::kSelect || reply.selected_index == 2) {
                        return make_operation_canceled<OperationResult>();
                    }
                    if (reply.selected_index == 1) {
                        nested_dirty_policy = NestedDirtyClosePolicy::kDiscard;
                    } else if (reply.selected_index == 0) {
                        nested_dirty_policy = NestedDirtyClosePolicy::kCommit;
                    } else {
                        return make_operation_canceled<OperationResult>();
                    }
                }
                if (nested_dirty_policy == NestedDirtyClosePolicy::kDiscard) {
                    ArchiveOpenSessionNativeAccess::set_dirty(*session, false);
                } else if (ArchiveOpenSessionNativeAccess::parent_generation_at_open(*session)
                           != ArchiveOpenSessionNativeAccess::generation(*parent)) {
                    return make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kIo,
                        "Nested archive changed after this session was opened; stale changes were preserved",
                        2);
                } else if (std::optional<OperationResult> commit_error =
                               commit_archive_session_to_parent(*session, hooks, cancel_requested, wait_while_paused);
                           commit_error.has_value()) {
                    return std::move(*commit_error);
                }
            } else {
                if (nested_dirty_policy == NestedDirtyClosePolicy::kDiscard) {
                    ArchiveOpenSessionNativeAccess::set_dirty(*session, false);
                } else {
                    if (std::optional<OperationResult> commit_error = commit_archive_session_to_root(*session);
                        commit_error.has_value()) {
                        return std::move(*commit_error);
                    }
                }
            }
        }

        std::shared_ptr<ArchiveOpenSession> dropped;
        ArchiveOpenSessionNativeAccess::set_closed(*session, true);
        {
            std::lock_guard<std::mutex> lock(registry.mutex_);
            auto it = registry.sessions_.find(token.value);
            if (it == registry.sessions_.end()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
            }
            dropped = std::move(it->second);
            registry.sessions_.erase(it);
        }
        dropped.reset();
        return make_operation_success<OperationResult>("Session closed");
    }

} // namespace z7::app
