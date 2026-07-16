// src/archive_application/src/native_7z/callbacks/callbacks_extract_result.cpp
// Role: Extract callback operation result accounting.

#include <cerrno>
#include <fstream>
#include <iterator>

#include "Windows/FileDir.h"
#include "Windows/FileIO.h"
#if defined(_WIN32) && !defined(UNDER_CE) && !defined(Z7_SFX)
#include "Windows/SecurityUtils.h"
#endif
#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"
#include "third_party_adapter/third_party_adapter.h"

#if defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stdio.h>
#include <sys/xattr.h>
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace z7::app {

    namespace {

        bool path_exists_no_follow(fs::path const& path, std::error_code& ec) {
            ec.clear();
            fs::file_status const status = fs::symlink_status(path, ec);
            if (ec == std::errc::no_such_file_or_directory) {
                ec.clear();
                return false;
            }
            return !ec && fs::status_known(status) && status.type() != fs::file_type::not_found;
        }

    } // namespace

    namespace {

#if defined(_WIN32) && !defined(UNDER_CE)

        fs::path zone_identifier_stream_path(fs::path const& base_path) {
            fs::path stream_path = base_path;
            stream_path += ":Zone.Identifier";
            return stream_path;
        }

        std::string read_zone_identifier_stream(fs::path const& base_path) {
            std::ifstream in(zone_identifier_stream_path(base_path), std::ios::binary);
            if (!in) {
                return {};
            }
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

#endif

#if (defined(_WIN32) && !defined(UNDER_CE)) || defined(__APPLE__)

        bool is_office_document_path(fs::path const& output_path) {
            std::string ext = output_path.extension().string();
            if (!ext.empty() && ext.front() == '.') {
                ext.erase(ext.begin());
            }
            for (char& ch : ext) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            static constexpr char const* kOfficeExtensions[] = {"doc",
                                                                "dot",
                                                                "wbk",
                                                                "docx",
                                                                "docm",
                                                                "dotx",
                                                                "dotm",
                                                                "docb",
                                                                "wll",
                                                                "wwl",
                                                                "xls",
                                                                "xlt",
                                                                "xlm",
                                                                "xlsx",
                                                                "xlsm",
                                                                "xltx",
                                                                "xltm",
                                                                "xlsb",
                                                                "xla",
                                                                "xlam",
                                                                "ppt",
                                                                "pot",
                                                                "pps",
                                                                "ppa",
                                                                "ppam",
                                                                "pptx",
                                                                "pptm",
                                                                "potx",
                                                                "potm",
                                                                "ppsx",
                                                                "ppsm",
                                                                "sldx",
                                                                "sldm"};
            for (char const* candidate : kOfficeExtensions) {
                if (ext == candidate) {
                    return true;
                }
            }
            return false;
        }

#endif

#if defined(_WIN32) && !defined(UNDER_CE)

        void write_zone_identifier_stream(fs::path const& output_path, std::string const& zone_data) {
            if (zone_data.empty()) {
                return;
            }
            std::ofstream out(zone_identifier_stream_path(output_path), std::ios::binary | std::ios::trunc);
            if (!out) {
                return;
            }
            out.write(zone_data.data(), static_cast<std::streamsize>(zone_data.size()));
        }

#endif

#if defined(__APPLE__)

        constexpr char const* kMacQuarantineAttributeName = "com.apple.quarantine";

        std::string read_quarantine_xattr(fs::path const& base_path) {
            std::string const native_path = base_path.string();
            errno = 0;
            ssize_t const size =
                ::getxattr(native_path.c_str(), kMacQuarantineAttributeName, nullptr, 0, 0, XATTR_NOFOLLOW);
            if (size <= 0) {
                return {};
            }

            std::string data(static_cast<size_t>(size), '\0');
            errno = 0;
            ssize_t const actual_size = ::getxattr(
                native_path.c_str(), kMacQuarantineAttributeName, data.data(), data.size(), 0, XATTR_NOFOLLOW);
            if (actual_size <= 0) {
                return {};
            }
            data.resize(static_cast<size_t>(actual_size));
            return data;
        }

        void write_quarantine_xattr(fs::path const& output_path, std::string const& quarantine_data) {
            if (quarantine_data.empty()) {
                return;
            }
            std::string const native_path = output_path.string();
            (void)::setxattr(native_path.c_str(),
                             kMacQuarantineAttributeName,
                             quarantine_data.data(),
                             quarantine_data.size(),
                             0,
                             XATTR_NOFOLLOW);
        }

#endif

        FString filesystem_path_to_fstring(fs::path const& path) {
            return us2fs(utf8_to_ustring(path.string()));
        }

        std::string normalize_link_target_separators(std::string target) {
            std::replace(target.begin(), target.end(), '\\', '/');
            std::string out;
            out.reserve(target.size());
            bool last_was_slash = false;
            for (char ch : target) {
                if (ch == '/') {
                    if (last_was_slash) {
                        continue;
                    }
                    last_was_slash = true;
                } else {
                    last_was_slash = false;
                }
                out.push_back(ch);
            }
            return out;
        }

        std::optional<std::string> normalize_archive_relative_link_target(std::string const& target) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= target.size()) {
                size_t const slash = target.find('/', start);
                std::string const token =
                    target.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!token.empty() && token != ".") {
                    if (token == "..") {
                        if (parts.empty()) {
                            return std::nullopt;
                        }
                        parts.pop_back();
                    } else {
                        parts.push_back(token);
                    }
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }

            std::string normalized;
            for (std::string const& part : parts) {
                if (!normalized.empty()) {
                    normalized.push_back('/');
                }
                normalized += part;
            }
            if (normalized.empty()) {
                return std::nullopt;
            }
            return normalized;
        }

        enum class HardLinkTargetState {
            kReady,
            kMissing,
            kInvalid
        };

        HardLinkTargetState inspect_hard_link_target(fs::path const& target_path, std::string& warning) {
            warning.clear();
            std::error_code ec;
            fs::file_status const status = fs::symlink_status(target_path, ec);
            if (ec == std::errc::no_such_file_or_directory) {
                return HardLinkTargetState::kMissing;
            }
            if (ec) {
                warning = "Cannot query hard link target: " + target_path.generic_string() + "; " + ec.message();
                return HardLinkTargetState::kInvalid;
            }
            if (!fs::status_known(status) || status.type() == fs::file_type::not_found) {
                return HardLinkTargetState::kMissing;
            }
            if (fs::is_symlink(status)) {
                warning = "Hard link target is a symbolic link and was skipped: " + target_path.generic_string();
                return HardLinkTargetState::kInvalid;
            }
            if (!fs::is_regular_file(status)) {
                warning = "Hard link target is not a regular file and was skipped: " + target_path.generic_string();
                return HardLinkTargetState::kInvalid;
            }
            return HardLinkTargetState::kReady;
        }

    } // namespace

    void NativeExtractCallback::record_nonfatal_warning(std::string const& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!diagnostic_message_.empty()) {
                diagnostic_message_ += '\n';
            }
            diagnostic_message_ += message;
        }
        emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kStdErr, message);
    }

    void NativeExtractCallback::record_partial_warning(std::string const& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++error_count_;
        }
        record_nonfatal_warning(message);
    }

    std::optional<std::string>
    NativeExtractCallback::materialized_collision_archive_entry(fs::path const& destination_path) const {
        std::vector<std::pair<fs::path, std::string>> prior_paths;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            prior_paths.reserve(materialized_entries_.size());
            for (ExtractMaterializedEntry const& entry : materialized_entries_) {
                prior_paths.emplace_back(fs::path(entry.absolute_output_path), entry.archive_entry_path);
            }
        }
        for (auto it = prior_paths.rbegin(); it != prior_paths.rend(); ++it) {
            std::error_code ec;
            if (fs::equivalent(destination_path, it->first, ec) && !ec) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    bool NativeExtractCallback::close_pending_entry_stream_locked(PendingEntry& pending_entry,
                                                                  std::string* close_error_message) {
        if (pending_entry.owned_stream == nullptr) {
            return true;
        }

        NativeFileOutStream* const stream = pending_entry.owned_stream;
        pending_entry.owned_stream = nullptr;
        pending_entry.bytes_written = stream->bytes_written();
        const HRESULT close_res = stream->Close();
        std::string const stream_failure = stream->failure_message();
        stream->Release();
        if (close_res == S_OK) {
            return true;
        }

        if (close_error_message != nullptr) {
            *close_error_message = stream_failure.empty() ? "Failed to finalize extracted output: "
                                                                + pending_entry.output_path.generic_string()
                                                          : stream_failure;
        }
        return false;
    }

    bool NativeExtractCallback::commit_pending_entry_locked(PendingEntry& pending_entry, std::string* error_message) {
        std::error_code auth_ec;
        if (!path_is_within_authorized_root(
                pending_entry.output_path.parent_path(), pending_entry.authorized_root, auth_ec)) {
            if (error_message != nullptr) {
                *error_message = "Output parent changed or resolves outside authorized extraction root: "
                               + pending_entry.output_path.generic_string()
                               + (auth_ec ? std::string("; ") + auth_ec.message() : "");
            }
            return false;
        }

        std::error_code ec;
        if (!filesystem_object_matches_identity_no_follow(pending_entry.temp_path, pending_entry.temp_identity, ec)) {
            if (error_message != nullptr) {
                *error_message = "Temporary extraction output changed before commit: "
                               + pending_entry.temp_path.generic_string()
                               + (ec ? std::string("; ") + ec.message() : "");
            }
            return false;
        }

        bool const exists_now = path_exists_no_follow(pending_entry.output_path, ec);
        if (ec) {
            if (error_message != nullptr) {
                *error_message = "Cannot query output before commit: "
                               + pending_entry.output_path.generic_string()
                               + "; "
                               + ec.message();
            }
            return false;
        }
        if (pending_entry.had_original != exists_now) {
            if (error_message != nullptr) {
                *error_message =
                    "Output changed while extraction was in progress: " + pending_entry.output_path.generic_string();
            }
            return false;
        }
        if (pending_entry.had_original
            && !filesystem_object_matches_identity_no_follow(
                pending_entry.output_path, pending_entry.original_identity, ec)) {
            if (error_message != nullptr) {
                *error_message = "Existing output was replaced while extraction was in progress: "
                               + pending_entry.output_path.generic_string()
                               + (ec ? std::string("; ") + ec.message() : "");
            }
            return false;
        }

        if (pending_entry.transaction == nullptr) {
            if (error_message != nullptr) {
                *error_message = "Extracted output has no filesystem transaction";
            }
            return false;
        }

        bool original_moved = false;
        if (pending_entry.had_original) {
            fs::path const requested_preserved_path = pending_entry.backup_path;
            TransactionMoveResult const backup =
                pending_entry.transaction->quarantine(pending_entry.output_path, &pending_entry.original_identity);
            if (!backup.success) {
                if (error_message != nullptr) {
                    *error_message = "Cannot preserve existing output before commit: "
                                   + pending_entry.output_path.generic_string()
                                   + "; "
                                   + backup.diagnostic;
                }
                return false;
            }
            pending_entry.backup_path = backup.preserved_path;
            original_moved = true;
            if (pending_entry.preserve_backup_on_commit) {
                TransactionMoveResult const preserved =
                    pending_entry.transaction->restore(pending_entry.backup_path, requested_preserved_path);
                if (!preserved.success) {
                    if (error_message != nullptr) {
                        *error_message = "Cannot preserve renamed existing output: " + preserved.diagnostic;
                    }
                    return false;
                }
                pending_entry.backup_path = requested_preserved_path;
                original_moved = false;
            }
        }

        TransactionMoveResult const promoted =
            pending_entry.transaction->promote(pending_entry.temp_path, pending_entry.output_path);
        if (promoted.success) {
            std::error_code identity_ec;
            if (filesystem_object_matches_identity_no_follow(
                    pending_entry.output_path, pending_entry.temp_identity, identity_ec)) {
                return true;
            }
            ec = identity_ec ? identity_ec : std::make_error_code(std::errc::state_not_recoverable);
        } else {
            ec = std::make_error_code(std::errc::io_error);
        }

        std::string restore_error;
        if (original_moved) {
            TransactionMoveResult const restored =
                pending_entry.transaction->restore(pending_entry.backup_path, pending_entry.output_path);
            if (!restored.success) {
                restore_error = "; " + restored.diagnostic;
            }
        } else if (pending_entry.had_original && pending_entry.preserve_backup_on_commit) {
            TransactionMoveResult const quarantined_failed =
                pending_entry.transaction->quarantine(pending_entry.backup_path, &pending_entry.original_identity);
            if (quarantined_failed.success) {
                TransactionMoveResult const restored =
                    pending_entry.transaction->restore(quarantined_failed.preserved_path, pending_entry.output_path);
                if (!restored.success) {
                    restore_error = "; " + restored.diagnostic;
                }
            } else {
                restore_error = "; " + quarantined_failed.diagnostic;
            }
        }
        if (error_message != nullptr) {
            *error_message = "Cannot commit extracted output: "
                           + pending_entry.output_path.generic_string()
                           + (ec ? std::string("; ") + ec.message() : "")
                           + restore_error;
        }
        return false;
    }

    void NativeExtractCallback::discard_pending_entry_locked(PendingEntry& pending_entry) {
        if (pending_entry.owned_stream != nullptr) {
            pending_entry.bytes_written = pending_entry.owned_stream->bytes_written();
            (void)pending_entry.owned_stream->Close();
            pending_entry.owned_stream->Release();
            pending_entry.owned_stream = nullptr;
        }
        if (budget_tracker_ != nullptr && pending_entry.bytes_written != 0) {
            budget_tracker_->release_bytes(pending_entry.bytes_written);
            pending_entry.bytes_written = 0;
        }
        if (pending_entry.budget_file_reserved) {
            release_budget_file();
            pending_entry.budget_file_reserved = false;
        }
        if (!pending_entry.temp_path.empty() && pending_entry.transaction != nullptr) {
            (void)pending_entry.transaction->discard(pending_entry.temp_path, &pending_entry.temp_identity);
        }
    }

    bool NativeExtractCallback::close_pending_alternate_stream(PendingAlternateStream& pending,
                                                               std::string* error_message) {
        if (pending.owned_stream == nullptr) {
            return true;
        }
        pending.bytes_written = pending.owned_stream->bytes_written();
        const HRESULT close_result = pending.owned_stream->Close();
        std::string const failure = pending.owned_stream->failure_message();
        pending.owned_stream->Release();
        pending.owned_stream = nullptr;
        if (close_result == S_OK && failure.empty()) {
            return true;
        }
        if (error_message != nullptr) {
            *error_message = failure.empty()
                               ? "Cannot close staged extended attribute: " + pending.archive_entry_path
                               : failure;
        }
        return false;
    }

    std::optional<std::string> NativeExtractCallback::commit_pending_alternate_stream(
        PendingAlternateStream const& pending) const {
#if defined(__APPLE__)
        std::error_code authorization_ec;
        if (!path_is_within_authorized_root(pending.output_path, pending.authorized_root, authorization_ec)) {
            return "Extended-attribute parent resolves outside authorized extraction root: "
                 + pending.output_path.generic_string()
                 + (authorization_ec ? std::string("; ") + authorization_ec.message() : "");
        }

        std::error_code identity_ec;
        if (!filesystem_object_matches_identity_no_follow(
                pending.output_path, pending.output_identity, identity_ec)) {
            return "Extended-attribute parent changed before commit: "
                 + pending.output_path.generic_string()
                 + (identity_ec ? std::string("; ") + identity_ec.message() : "");
        }
        if (!filesystem_object_matches_identity_no_follow(pending.temp_path, pending.temp_identity, identity_ec)) {
            return "Staged extended attribute changed before commit: "
                 + pending.temp_path.generic_string()
                 + (identity_ec ? std::string("; ") + identity_ec.message() : "");
        }

        int const temp_fd = ::open(pending.temp_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (temp_fd < 0) {
            return "Cannot open staged extended attribute: "
                 + pending.temp_path.generic_string()
                 + "; "
                 + std::generic_category().message(errno);
        }
        struct stat temp_info{};
        if (::fstat(temp_fd, &temp_info) != 0
            || static_cast<uint64_t>(temp_info.st_dev) != pending.temp_identity.volume
            || static_cast<uint64_t>(temp_info.st_ino) != pending.temp_identity.object
            || temp_info.st_size < 0
            || static_cast<uint64_t>(temp_info.st_size) > std::numeric_limits<size_t>::max()) {
            int const saved_errno = errno;
            (void)::close(temp_fd);
            return "Cannot validate staged extended attribute: "
                 + pending.temp_path.generic_string()
                 + (saved_errno != 0 ? "; " + std::generic_category().message(saved_errno) : "");
        }

        size_t const value_size = static_cast<size_t>(temp_info.st_size);
        void* mapped = nullptr;
        if (value_size != 0) {
            mapped = ::mmap(nullptr, value_size, PROT_READ, MAP_PRIVATE, temp_fd, 0);
            if (mapped == MAP_FAILED) {
                int const saved_errno = errno;
                (void)::close(temp_fd);
                return "Cannot map staged extended attribute: "
                     + pending.temp_path.generic_string()
                     + "; "
                     + std::generic_category().message(saved_errno);
            }
        }

        // O_SYMLINK opens the link object itself when the final component is a
        // symlink, and behaves like a normal open for files/directories. The
        // subsequent fstat identity check binds the xattr write to the object
        // that was materialized by this extraction.
        int const output_fd = ::open(pending.output_path.c_str(), O_RDONLY | O_SYMLINK | O_CLOEXEC);
        if (output_fd < 0) {
            int const saved_errno = errno;
            if (mapped != nullptr) {
                (void)::munmap(mapped, value_size);
            }
            (void)::close(temp_fd);
            return "Cannot open extended-attribute parent: "
                 + pending.output_path.generic_string()
                 + "; "
                 + std::generic_category().message(saved_errno);
        }

        struct stat output_info{};
        bool const output_matches = ::fstat(output_fd, &output_info) == 0
                                 && static_cast<uint64_t>(output_info.st_dev) == pending.output_identity.volume
                                 && static_cast<uint64_t>(output_info.st_ino) == pending.output_identity.object;
        int set_result = -1;
        int set_error = 0;
        if (output_matches) {
            errno = 0;
            set_result = ::fsetxattr(output_fd,
                                     pending.attribute_name.c_str(),
                                     value_size == 0 ? nullptr : mapped,
                                     value_size,
                                     0,
                                     0);
            set_error = errno;
        }
        int const output_close_result = ::close(output_fd);
        int const unmap_result = mapped == nullptr ? 0 : ::munmap(mapped, value_size);
        int const temp_close_result = ::close(temp_fd);

        if (!output_matches) {
            return "Extended-attribute parent identity changed during commit: "
                 + pending.output_path.generic_string();
        }
        if (set_result != 0) {
            return "Cannot restore extended attribute '"
                 + pending.attribute_name
                 + "' on "
                 + pending.output_path.generic_string()
                 + "; "
                 + std::generic_category().message(set_error);
        }
        if (output_close_result != 0 || unmap_result != 0 || temp_close_result != 0) {
            return "Cannot finish extended-attribute commit for: " + pending.output_path.generic_string();
        }
        return std::nullopt;
#else
        (void)pending;
        return "Native extended-attribute restoration is unavailable on this platform";
#endif
    }

    std::optional<std::string> NativeExtractCallback::discard_pending_alternate_stream(
        PendingAlternateStream& pending, bool release_budget_bytes) {
        std::string diagnostic;
        std::string close_error;
        if (!close_pending_alternate_stream(pending, &close_error)) {
            diagnostic = std::move(close_error);
        }
        if (release_budget_bytes && budget_tracker_ != nullptr && pending.bytes_written != 0) {
            budget_tracker_->release_bytes(pending.bytes_written);
            pending.bytes_written = 0;
        }
        if (pending.transaction != nullptr && !pending.temp_path.empty()) {
            TransactionMoveResult const discarded =
                pending.transaction->discard(pending.temp_path, &pending.temp_identity);
            if (!discarded.success) {
                if (!diagnostic.empty()) {
                    diagnostic += "; ";
                }
                diagnostic += discarded.diagnostic;
            }
            std::string finish_diagnostic;
            if (!pending.transaction->finish(&finish_diagnostic)) {
                if (!diagnostic.empty()) {
                    diagnostic += "; ";
                }
                diagnostic += finish_diagnostic;
            }
        }
        if (diagnostic.empty()) {
            return std::nullopt;
        }
        return diagnostic;
    }

    bool NativeExtractCallback::cleanup_materialized_target_locked(
        OutputTarget const& target, FilesystemObjectIdentity const& materialized_identity, std::string* error_message) {
        if (target.transaction == nullptr) {
            if (error_message != nullptr) {
                *error_message = "Failed extraction output has no owning filesystem transaction";
            }
            return false;
        }
        std::error_code ec;
        bool const output_exists = path_exists_no_follow(target.output_path, ec);
        if (ec) {
            if (error_message != nullptr) {
                *error_message = "Cannot inspect failed extraction output during cleanup: "
                               + target.output_path.generic_string()
                               + "; "
                               + ec.message();
            }
            return false;
        }
        fs::path quarantined_output;
        if (output_exists) {
            TransactionMoveResult const quarantined =
                target.transaction->quarantine(target.output_path, &materialized_identity);
            if (!quarantined.success) {
                if (error_message != nullptr) {
                    *error_message = "Failed extraction output changed before cleanup and was preserved: "
                                   + quarantined.diagnostic;
                }
                return false;
            }
            quarantined_output = quarantined.preserved_path;
        }

        if (target.had_original && !target.backup_path.empty()) {
            fs::path owned_backup = target.backup_path;
            if (owned_backup.parent_path().lexically_normal()
                != target.transaction->directory().lexically_normal()) {
                TransactionMoveResult const quarantined_backup =
                    target.transaction->quarantine(owned_backup, &target.original_identity);
                if (!quarantined_backup.success) {
                    if (error_message != nullptr) {
                        *error_message = "Original output backup changed and was not restored: "
                                       + quarantined_backup.diagnostic;
                    }
                    return false;
                }
                owned_backup = quarantined_backup.preserved_path;
            }
            TransactionMoveResult const restored = target.transaction->restore(owned_backup, target.output_path);
            if (!restored.success) {
                if (error_message != nullptr) {
                    *error_message = "Cannot restore original extraction output: " + restored.diagnostic;
                }
                return false;
            }
        }

        if (!quarantined_output.empty()) {
            TransactionMoveResult const discarded =
                target.transaction->discard(quarantined_output, &materialized_identity);
            if (!discarded.success) {
                if (error_message != nullptr) {
                    *error_message = discarded.diagnostic;
                }
                return false;
            }
        }
        return true;
    }

    HRESULT NativeExtractCallback::read_item_attributes(UInt32 index, ExtractItemAttributes& attributes) const {
        attributes = ExtractItemAttributes{};

        {
            NWindows::NCOM::CPropVariant prop;
            const HRESULT hr = archive_->GetProperty(index, kpidPosixAttrib, &prop);
            if (hr != S_OK) {
                return hr;
            }
            if (prop.vt == VT_UI4) {
                attributes.defined = true;
                attributes.attrib = static_cast<UInt32>((prop.ulVal << 16) | FILE_ATTRIBUTE_UNIX_EXTENSION);
            } else if (prop.vt != VT_EMPTY) {
                return E_FAIL;
            }
        }

        {
            NWindows::NCOM::CPropVariant prop;
            const HRESULT hr = archive_->GetProperty(index, kpidAttrib, &prop);
            if (hr != S_OK) {
                return hr;
            }
            if (prop.vt == VT_UI4) {
                if (!attributes.defined || (prop.ulVal & FILE_ATTRIBUTE_UNIX_EXTENSION) != 0) {
                    attributes.defined = true;
                    attributes.attrib = prop.ulVal;
                }
            } else if (prop.vt != VT_EMPTY) {
                return E_FAIL;
            }
        }

        return S_OK;
    }

    HRESULT NativeExtractCallback::read_item_times(UInt32 index, ExtractItemTimes& times) const {
        times = ExtractItemTimes{};
        auto read_time = [&](PROPID prop_id, CFiTime& value, bool& defined) -> HRESULT {
            NWindows::NCOM::CPropVariant prop;
            const HRESULT hr = archive_->GetProperty(index, prop_id, &prop);
            if (hr != S_OK) {
                return hr;
            }
            if (prop.vt == VT_EMPTY) {
                return S_OK;
            }
            if (prop.vt != VT_FILETIME) {
                return E_FAIL;
            }
#ifdef _WIN32
            FILETIME_To_FiTime(prop.filetime, value);
#else
            if (!FILETIME_To_timespec(prop.filetime, value)) {
                return E_FAIL;
            }
#endif
            defined = true;
            return S_OK;
        };

        HRESULT hr = read_time(kpidCTime, times.ctime, times.ctime_defined);
        if (hr != S_OK) {
            return hr;
        }
        hr = read_time(kpidATime, times.atime, times.atime_defined);
        if (hr != S_OK) {
            return hr;
        }
        return read_time(kpidMTime, times.mtime, times.mtime_defined);
    }

    HRESULT NativeExtractCallback::read_item_link_info(UInt32 index, ExtractItemLinkInfo& link_info) const {
        link_info = ExtractItemLinkInfo{};

        {
            NWindows::NCOM::CPropVariant prop;
            const HRESULT hr = archive_->GetProperty(index, kpidHardLink, &prop);
            if (hr != S_OK) {
                return hr;
            }
            if (prop.vt == VT_BSTR) {
                UString target;
                target.SetFromBstr(prop.bstrVal);
                link_info.type = ExtractItemLinkInfo::Type::kHardLink;
                link_info.target = ustring_to_utf8(target);
            } else if (prop.vt != VT_EMPTY) {
                return E_FAIL;
            }
        }

        {
            NWindows::NCOM::CPropVariant prop;
            const HRESULT hr = archive_->GetProperty(index, kpidSymLink, &prop);
            if (hr != S_OK) {
                return hr;
            }
            if (prop.vt == VT_BSTR) {
                UString target;
                target.SetFromBstr(prop.bstrVal);
                link_info.type = ExtractItemLinkInfo::Type::kSymLink;
                link_info.target = ustring_to_utf8(target);
            } else if (prop.vt != VT_EMPTY) {
                return E_FAIL;
            }
        }

        return S_OK;
    }

    HRESULT NativeExtractCallback::read_item_alternate_stream_info(
        UInt32 index, ExtractItemAlternateStreamInfo& info) const {
        info = ExtractItemAlternateStreamInfo{};
        bool is_alternate_stream = false;
        if (!archive_get_prop_bool(archive_, index, kpidIsAltStream, is_alternate_stream)
            || !is_alternate_stream) {
            return S_OK;
        }

        CMyComPtr<IArchiveGetRawProps> raw_props;
        if (archive_->QueryInterface(IID_IArchiveGetRawProps, reinterpret_cast<void**>(&raw_props)) != S_OK
            || !raw_props) {
            return E_NOINTERFACE;
        }

        UInt32 parent_index = static_cast<UInt32>(-1);
        UInt32 parent_type = NParentType::kDir;
        const HRESULT parent_result = raw_props->GetParent(index, &parent_index, &parent_type);
        if (parent_result != S_OK) {
            return parent_result;
        }
        if (parent_type != NParentType::kAltStream || parent_index == static_cast<UInt32>(-1)) {
            return E_FAIL;
        }

        std::string attribute_name = archive_get_prop_text(archive_, index, kpidName);
        if (attribute_name.empty()) {
            return E_FAIL;
        }
        if (attribute_name == "rsrc") {
            attribute_name = "com.apple.ResourceFork";
        }

        info.is_alternate_stream = true;
        info.parent_index = parent_index;
        info.attribute_name = std::move(attribute_name);
        return S_OK;
    }

    std::optional<std::string> NativeExtractCallback::apply_item_attributes(fs::path const& output_path,
                                                                            ExtractItemAttributes const& attributes,
                                                                            bool finalize_directory) const {
        if (!attributes.defined) {
            return std::nullopt;
        }

        FString const native_path = filesystem_path_to_fstring(output_path);
        if (!NWindows::NFile::NDir::SetFileAttrib_PosixHighDetect(native_path, attributes.attrib)) {
            return "Cannot set file attribute: " + output_path.generic_string();
        }
#ifndef _WIN32
        if (finalize_directory
            && (attributes.attrib & FILE_ATTRIBUTE_UNIX_EXTENSION) != 0
            && MY_LIN_S_ISDIR(attributes.attrib >> 16)) {
            std::error_code ec;
            fs::perms const current = fs::status(output_path, ec).permissions();
            if (ec) {
                return "Cannot inspect final directory permissions: "
                     + output_path.generic_string()
                     + "; "
                     + ec.message();
            }
            fs::perms const archived = static_cast<fs::perms>((attributes.attrib >> 16) & 0777);
            fs::permissions(output_path, current & archived, fs::perm_options::replace, ec);
            if (ec) {
                return "Cannot finalize directory permissions: " + output_path.generic_string() + "; " + ec.message();
            }
        }
#else
        (void)finalize_directory;
#endif
        return std::nullopt;
    }

    std::optional<std::string> NativeExtractCallback::apply_item_times(fs::path const& output_path,
                                                                       ExtractItemTimes const& times,
                                                                       bool is_symlink) const {
        if (!times.ctime_defined && !times.atime_defined && !times.mtime_defined) {
            return std::nullopt;
        }
        FString const native_path = filesystem_path_to_fstring(output_path);
        bool applied = false;
        if (is_symlink) {
            applied = NWindows::NFile::NDir::SetLinkFileTime(native_path,
                                                             times.ctime_defined ? &times.ctime : nullptr,
                                                             times.atime_defined ? &times.atime : nullptr,
                                                             times.mtime_defined ? &times.mtime : nullptr);
        } else {
            applied = NWindows::NFile::NDir::SetDirTime(native_path,
                                                        times.ctime_defined ? &times.ctime : nullptr,
                                                        times.atime_defined ? &times.atime : nullptr,
                                                        times.mtime_defined ? &times.mtime : nullptr);
        }
        if (applied) {
            return std::nullopt;
        }
        return "Cannot set file time: " + output_path.generic_string();
    }

    std::optional<std::string> NativeExtractCallback::apply_item_security(UInt32 index,
                                                                          fs::path const& output_path) const {
        if (!restore_file_security_) {
            return std::nullopt;
        }
#if defined(_WIN32) && !defined(UNDER_CE) && !defined(Z7_SFX)
        CMyComPtr<IArchiveGetRawProps> raw_props;
        if (archive_->QueryInterface(IID_IArchiveGetRawProps, reinterpret_cast<void**>(&raw_props)) != S_OK
            || !raw_props) {
            return "Archive does not expose restorable security information: " + output_path.generic_string();
        }
        void const* data = nullptr;
        UInt32 data_size = 0;
        UInt32 prop_type = 0;
        const HRESULT hr = raw_props->GetRawProp(index, kpidNtSecure, &data, &data_size, &prop_type);
        if (hr != S_OK) {
            return "Cannot read archived security information: " + output_path.generic_string();
        }
        if (data_size == 0) {
            return std::nullopt;
        }
        if (prop_type != NPropDataType::kRaw || !CheckNtSecure(static_cast<Byte const*>(data), data_size)) {
            return "Archived security descriptor is invalid: " + output_path.generic_string();
        }
        SECURITY_INFORMATION const info =
            DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION;
        if (::SetFileSecurityW(
                output_path.c_str(), info, reinterpret_cast<PSECURITY_DESCRIPTOR>(const_cast<void*>(data)))) {
            return std::nullopt;
        }
        return "Cannot restore file security: "
             + output_path.generic_string()
             + "; "
             + std::system_category().message(static_cast<int>(::GetLastError()));
#elif defined(_WIN32)
        return "File security restoration is unavailable in this build: " + output_path.generic_string();
#else
        (void)index;
        (void)output_path;
        return std::nullopt;
#endif
    }

    std::optional<std::string> NativeExtractCallback::prepare_link_creation_plan(OutputTarget const& output_target,
                                                                                 ExtractItemLinkInfo const& link_info,
                                                                                 LinkCreationPlan& plan) const {
        plan = LinkCreationPlan{};
        std::string target = normalize_link_target_separators(link_info.target);
        if (target.empty()) {
            return "Empty link target was skipped: " + output_target.archive_entry_path;
        }
        if (is_absolute_item_path(target)) {
            return "Unsafe absolute link target was skipped: " + output_target.archive_entry_path + " -> " + target;
        }

        if (link_info.type == ExtractItemLinkInfo::Type::kSymLink) {
            fs::path const resolved_target =
                (output_target.output_path.parent_path() / fs::path(target)).lexically_normal();
            std::error_code ec;
            if (!path_is_within_authorized_root(resolved_target, output_target.authorized_root, ec)) {
                return "Unsafe symbolic link target was skipped: "
                     + output_target.archive_entry_path
                     + " -> "
                     + target
                     + (ec ? std::string("; ") + ec.message() : "");
            }
            plan.type = ExtractItemLinkInfo::Type::kSymLink;
            plan.symlink_target = std::move(target);
            return std::nullopt;
        }

        if (link_info.type == ExtractItemLinkInfo::Type::kHardLink) {
            std::optional<std::string> const normalized_target = normalize_archive_relative_link_target(target);
            if (!normalized_target.has_value()
                || !archive_virtual_path_is_safe_for_materialization(*normalized_target)) {
                return "Unsafe hard link target was skipped: " + output_target.archive_entry_path + " -> " + target;
            }

            ResolvedPath const resolved_target = resolve_destination_path(*normalized_target, false);
            std::error_code ec;
            if (!path_is_within_authorized_root(
                    resolved_target.destination_path, resolved_target.authorized_root, ec)) {
                return "Hard link target outside extraction root was skipped: "
                     + output_target.archive_entry_path
                     + " -> "
                     + *normalized_target
                     + (ec ? std::string("; ") + ec.message() : "");
            }

            plan.type = ExtractItemLinkInfo::Type::kHardLink;
            plan.hardlink_target_path = resolved_target.destination_path;
            return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<std::string> NativeExtractCallback::create_symbolic_link(fs::path const& output_path,
                                                                           std::string const& target) const {
#if defined(_WIN32)
        (void)output_path;
        (void)target;
        return "Symbolic link extraction is not supported on this platform.";
#else
        FString const native_output_path = filesystem_path_to_fstring(output_path);
        if (NWindows::NFile::NIO::SetSymLink_UString(native_output_path, utf8_to_ustring(target))) {
            return std::nullopt;
        }
        return "Cannot create symbolic link: " + output_path.generic_string() + " -> " + target;
#endif
    }

    std::optional<std::string> NativeExtractCallback::create_hard_link(fs::path const& output_path,
                                                                       fs::path const& target_path) const {
#if defined(_WIN32)
        (void)output_path;
        (void)target_path;
        return "Hard link extraction is not supported on this platform.";
#else
        int const target_parent = ::open(target_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (target_parent < 0) {
            return "Cannot anchor hard link target parent: " + target_path.generic_string() + "; "
                 + std::error_code(errno, std::generic_category()).message();
        }
        int const output_parent = ::open(output_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (output_parent < 0) {
            std::error_code const open_error(errno, std::generic_category());
            ::close(target_parent);
            return "Cannot anchor hard link output parent: " + output_path.generic_string() + "; "
                 + open_error.message();
        }
        struct stat target_status {};
        bool const target_is_regular =
            ::fstatat(target_parent, target_path.filename().c_str(), &target_status, AT_SYMLINK_NOFOLLOW) == 0
            && S_ISREG(target_status.st_mode);
        bool const linked = target_is_regular
                         && ::linkat(target_parent,
                                     target_path.filename().c_str(),
                                     output_parent,
                                     output_path.filename().c_str(),
                                     0)
                                == 0;
        int const link_error = errno;
        ::close(output_parent);
        ::close(target_parent);
        if (linked) {
            return std::nullopt;
        }
        return "Cannot create anchored hard link: " + output_path.generic_string() + " -> "
             + target_path.generic_string() + "; "
             + (target_is_regular ? std::error_code(link_error, std::generic_category()).message()
                                  : "target is missing, symbolic, or not a regular file");
#endif
    }

    void NativeExtractCallback::remember_materialized_output_locked(
        UInt32 archive_index,
        fs::path const& output_path,
        fs::path const& authorized_root,
        FilesystemObjectIdentity const& identity,
        ExtractItemTimes const& times,
        bool is_directory,
        bool is_symlink) {
        MaterializedOutputTarget target;
        target.output_path = output_path;
        target.authorized_root = authorized_root;
        target.identity = identity;
        target.times = times;
        target.is_directory = is_directory;
        target.is_symlink = is_symlink;
        materialized_output_targets_[archive_index] = std::move(target);
        skipped_archive_indices_.erase(archive_index);
    }

    void NativeExtractCallback::remember_skipped_archive_item(UInt32 archive_index) {
        std::lock_guard<std::mutex> lock(mutex_);
        skipped_archive_indices_.insert(archive_index);
        materialized_output_targets_.erase(archive_index);
    }

    void NativeExtractCallback::record_materialized_output_locked(OutputTarget const& target,
                                                                  uint64_t bytes_written,
                                                                  bool is_directory,
                                                                  FilesystemObjectIdentity const& output_identity,
                                                                  ExtractItemTimes const& times) {
        if (target.had_original || target.overwrote_existing) {
            std::string collided_archive_entry = target.collided_archive_entry_path;
            std::string collided_output_path;
            if (collided_archive_entry.empty()) {
                auto const prior_rollback = std::find_if(
                    rollback_entries_.rbegin(), rollback_entries_.rend(), [&](ExtractRollbackEntry const& entry) {
                        return entry.output_identity.defined && target.original_identity.defined
                            && entry.output_identity.volume == target.original_identity.volume
                            && entry.output_identity.object == target.original_identity.object;
                    });
                if (prior_rollback != rollback_entries_.rend()) {
                    collided_output_path = fs::absolute(prior_rollback->output_path).generic_string();
                }
            }
            auto reverse_prior = std::find_if(
                materialized_entries_.rbegin(),
                materialized_entries_.rend(),
                [&](ExtractMaterializedEntry const& entry) {
                    if ((!collided_archive_entry.empty() && entry.archive_entry_path == collided_archive_entry)
                        || (!collided_output_path.empty() && entry.absolute_output_path == collided_output_path)) {
                        return true;
                    }
                    if (!target.overwrote_existing) {
                        return false;
                    }
                    fs::path const prior_path(entry.absolute_output_path);
                    if (prior_path.parent_path().lexically_normal()
                        != fs::absolute(target.output_path).parent_path().lexically_normal()) {
                        return false;
                    }
                    std::string prior_name = prior_path.filename().string();
                    std::string target_name = target.output_path.filename().string();
                    auto lower_ascii = [](std::string& value) {
                        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                            return static_cast<char>(std::tolower(ch));
                        });
                    };
                    lower_ascii(prior_name);
                    lower_ascii(target_name);
                    return prior_name == target_name;
                });
            auto prior = reverse_prior == materialized_entries_.rend()
                           ? materialized_entries_.end()
                           : std::prev(reverse_prior.base());
            if (prior != materialized_entries_.end()) {
                if (target.preserve_backup_on_commit) {
                    prior->absolute_output_path = fs::absolute(target.backup_path).generic_string();
                    prior->renamed_from_collision = true;
                } else {
                    materialized_entries_.erase(prior);
                }
            }
        }

        ExtractMaterializedEntry materialized_entry;
        materialized_entry.archive_entry_path = target.archive_entry_path;
        materialized_entry.absolute_output_path = target.absolute_output_path;
        materialized_entry.is_directory = is_directory;
        materialized_entry.bytes_written = bytes_written;
        materialized_entry.overwrote_existing = target.overwrote_existing;
        materialized_entry.renamed_from_collision = target.renamed_from_collision;
        materialized_entries_.push_back(std::move(materialized_entry));
        remember_materialized_output_locked(
            target.archive_index, target.output_path, target.authorized_root, output_identity, times, is_directory);

        ExtractRollbackEntry rollback_entry;
        rollback_entry.output_path = target.output_path;
        rollback_entry.destination_path = target.destination_path;
        rollback_entry.backup_path = target.backup_path;
        rollback_entry.transaction = target.transaction;
        rollback_entry.had_original = target.had_original;
        rollback_entry.preserve_backup_on_commit = target.preserve_backup_on_commit;
        rollback_entry.is_directory = is_directory;
        rollback_entry.remove_only_if_empty = is_directory;
        rollback_entry.output_identity = output_identity;
        if (target.had_original && !target.backup_path.empty()) {
            rollback_entry.backup_identity = target.original_identity;
        }
        rollback_entries_.push_back(std::move(rollback_entry));
    }

    std::optional<std::string>
    NativeExtractCallback::materialize_link(OutputTarget const& output_target,
                                            LinkCreationPlan const& plan,
                                            bool allow_defer,
                                            FilesystemObjectIdentity* materialized_identity) {
        if (materialized_identity != nullptr) {
            *materialized_identity = FilesystemObjectIdentity{};
        }
        OutputTarget committed_target = output_target;
        auto commit_link = [&](auto&& create) -> std::optional<std::string> {
            std::error_code auth_ec;
            if (!path_is_within_authorized_root(
                    output_target.output_path.parent_path(), output_target.authorized_root, auth_ec)) {
                return "Link output parent changed or resolves outside extraction root: "
                     + output_target.output_path.generic_string()
                     + (auth_ec ? std::string("; ") + auth_ec.message() : "");
            }

            std::error_code ec;
            bool const exists_now = path_exists_no_follow(output_target.output_path, ec);
            if (ec || exists_now != output_target.had_original) {
                return "Link output changed while extraction was in progress: "
                     + output_target.output_path.generic_string()
                     + (ec ? std::string("; ") + ec.message() : "");
            }
            if (output_target.had_original
                && !filesystem_object_matches_identity_no_follow(
                    output_target.output_path, output_target.original_identity, ec)) {
                return "Existing link output was replaced while extraction was in progress: "
                     + output_target.output_path.generic_string()
                     + (ec ? std::string("; ") + ec.message() : "");
            }

            if (committed_target.transaction == nullptr) {
                return "Link output has no filesystem transaction: " + committed_target.output_path.generic_string();
            }

            bool original_moved = false;
            if (committed_target.had_original) {
                fs::path const requested_preserved_path = committed_target.backup_path;
                TransactionMoveResult const backup = committed_target.transaction->quarantine(
                    committed_target.output_path, &committed_target.original_identity);
                if (!backup.success) {
                    return "Cannot preserve existing link output: " + backup.diagnostic;
                }
                committed_target.backup_path = backup.preserved_path;
                original_moved = true;
                if (committed_target.preserve_backup_on_commit) {
                    TransactionMoveResult const preserved = committed_target.transaction->restore(
                        committed_target.backup_path, requested_preserved_path);
                    if (!preserved.success) {
                        return "Cannot rename existing link output: " + preserved.diagnostic;
                    }
                    committed_target.backup_path = requested_preserved_path;
                    original_moved = false;
                }
            }

            if (std::optional<std::string> warning = create(); !warning.has_value()) {
                return std::nullopt;
            } else {
                if (original_moved) {
                    TransactionMoveResult const restored = committed_target.transaction->restore(
                        committed_target.backup_path, committed_target.output_path);
                    if (!restored.success) {
                        *warning += "; " + restored.diagnostic;
                    }
                }
                return warning;
            }
        };

        if (plan.type == ExtractItemLinkInfo::Type::kSymLink) {
            std::optional<std::string> warning =
                commit_link([&]() { return create_symbolic_link(output_target.output_path, plan.symlink_target); });
            if (warning.has_value()) {
                return warning;
            }
            std::error_code identity_ec;
            FilesystemObjectIdentity const output_identity =
                capture_filesystem_object_identity_no_follow(output_target.output_path, identity_ec);
            if (identity_ec || !output_identity.defined) {
                return "Cannot identify created symbolic link: "
                     + output_target.output_path.generic_string()
                     + (identity_ec ? std::string("; ") + identity_ec.message() : "");
            }
            std::lock_guard<std::mutex> lock(mutex_);
            record_materialized_output_locked(
                committed_target, 0, false, output_identity, ExtractItemTimes{});
            if (materialized_identity != nullptr) {
                *materialized_identity = output_identity;
            }
            return std::nullopt;
        }

        if (plan.type == ExtractItemLinkInfo::Type::kHardLink) {
            std::error_code authorization_ec;
            if (!path_is_within_authorized_root(
                    plan.hardlink_target_path, output_target.authorized_root, authorization_ec)) {
                return "Hard link target changed or resolves outside extraction root and was skipped: "
                     + plan.hardlink_target_path.generic_string()
                     + (authorization_ec ? std::string("; ") + authorization_ec.message() : "");
            }
            std::string target_warning;
            HardLinkTargetState const target_state =
                inspect_hard_link_target(plan.hardlink_target_path, target_warning);
            if (target_state == HardLinkTargetState::kMissing) {
                if (allow_defer) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    DeferredHardLink deferred;
                    deferred.output_target = output_target;
                    deferred.target_path = plan.hardlink_target_path;
                    deferred_hard_links_.push_back(std::move(deferred));
                    return std::nullopt;
                }
                return "Hard link target is missing and was skipped: "
                     + output_target.archive_entry_path
                     + " -> "
                     + plan.hardlink_target_path.generic_string();
            }
            if (target_state == HardLinkTargetState::kInvalid) {
                return target_warning;
            }

            std::optional<std::string> warning =
                commit_link([&]() { return create_hard_link(output_target.output_path, plan.hardlink_target_path); });
            if (warning.has_value()) {
                return warning;
            }
            std::error_code identity_ec;
            FilesystemObjectIdentity const output_identity =
                capture_filesystem_object_identity_no_follow(output_target.output_path, identity_ec);
            if (identity_ec || !output_identity.defined) {
                return "Cannot identify created hard link: "
                     + output_target.output_path.generic_string()
                     + (identity_ec ? std::string("; ") + identity_ec.message() : "");
            }
            std::lock_guard<std::mutex> lock(mutex_);
            record_materialized_output_locked(
                committed_target, 0, false, output_identity, ExtractItemTimes{});
            if (materialized_identity != nullptr) {
                *materialized_identity = output_identity;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string>
    NativeExtractCallback::materialize_data_symlink(PendingDataSymlink const& pending,
                                                     FilesystemObjectIdentity* materialized_identity) {
        if (pending.target_data.empty() || pending.target_data.size() >= kDataSymlinkLimit) {
            return "Invalid data-stream symbolic link target was skipped: "
                 + pending.output_target.archive_entry_path;
        }
        if (std::find(pending.target_data.begin(), pending.target_data.end(), uint8_t{0})
            != pending.target_data.end()) {
            return "Symbolic link target contains a NUL byte and was skipped: "
                 + pending.output_target.archive_entry_path;
        }

        AString utf8;
        utf8.SetFrom_CalcLen(reinterpret_cast<char const*>(pending.target_data.data()),
                            static_cast<unsigned>(pending.target_data.size()));
        UString unicode_target;
        if (!ConvertUTF8ToUnicode(utf8, unicode_target) || unicode_target.IsEmpty()) {
            return "Symbolic link target is not valid UTF-8 and was skipped: "
                 + pending.output_target.archive_entry_path;
        }

        ExtractItemLinkInfo link_info;
        link_info.type = ExtractItemLinkInfo::Type::kSymLink;
        link_info.target = ustring_to_utf8(unicode_target);
        LinkCreationPlan plan;
        if (std::optional<std::string> const warning =
                prepare_link_creation_plan(pending.output_target, link_info, plan);
            warning.has_value()) {
            return warning;
        }
        return materialize_link(pending.output_target, plan, false, materialized_identity);
    }

    HRESULT NativeExtractCallback::finalize_unreported_item_if_needed() {
        bool has_pending_item = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            has_pending_item = pending_entry_.has_value()
                            || pending_directory_.has_value()
                            || pending_link_.has_value()
                            || pending_data_symlink_.has_value()
                            || pending_alternate_stream_.has_value()
                            || pending_deferred_hard_link_output_.has_value();
        }
        return has_pending_item ? SetOperationResult(NArchive::NExtract::NOperationResult::kOK) : S_OK;
    }

    void NativeExtractCallback::finish_deferred_links() {
        (void)finalize_unreported_item_if_needed();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Some handlers complete a no-stream item without a matching
            // SetOperationResult call. Extract() returning S_OK is the final
            // confirmation for such a link.
            if (pending_link_.has_value()) {
                PendingLink const& pending = *pending_link_;
                std::error_code identity_ec;
                if (!filesystem_object_matches_identity_no_follow(
                        pending.output_target.output_path, pending.materialized_identity, identity_ec)) {
                    if (!diagnostic_message_.empty()) {
                        diagnostic_message_ += '\n';
                    }
                    diagnostic_message_ += "Created output link changed before extraction completed: "
                                         + pending.output_target.output_path.generic_string()
                                         + (identity_ec ? std::string("; ") + identity_ec.message() : "");
                    ++error_count_;
                    io_error_ = true;
                    if (!materialized_entries_.empty()
                        && materialized_entries_.back().absolute_output_path
                               == pending.output_target.absolute_output_path) {
                        materialized_entries_.pop_back();
                    }
                    if (!rollback_entries_.empty()
                        && rollback_entries_.back().output_path == pending.output_target.output_path) {
                        rollback_entries_.pop_back();
                    }
                    materialized_output_targets_.erase(pending.output_target.archive_index);
                    release_budget_file();
                } else {
                    if (std::optional<std::string> const warning =
                            apply_item_times(pending.output_target.output_path, pending.times, pending.is_symlink);
                        warning.has_value()) {
                        if (!diagnostic_message_.empty()) {
                            diagnostic_message_ += '\n';
                        }
                        diagnostic_message_ += *warning;
                        ++error_count_;
                    }
                    if (!pending.is_symlink) {
                        if (std::optional<std::string> const warning =
                                apply_item_attributes(pending.output_target.output_path, pending.attributes);
                            warning.has_value()) {
                            if (!diagnostic_message_.empty()) {
                                diagnostic_message_ += '\n';
                            }
                            diagnostic_message_ += *warning;
                            ++error_count_;
                        }
                    }
                    if (std::optional<std::string> const warning =
                            apply_item_security(pending.output_target.archive_index, pending.output_target.output_path);
                        warning.has_value()) {
                        if (!diagnostic_message_.empty()) {
                            diagnostic_message_ += '\n';
                        }
                        diagnostic_message_ += *warning;
                        ++error_count_;
                    }
                    remember_materialized_output_locked(pending.output_target.archive_index,
                                                        pending.output_target.output_path,
                                                        pending.output_target.authorized_root,
                                                        pending.materialized_identity,
                                                        pending.times,
                                                        false,
                                                        pending.is_symlink);
                }
                pending_link_.reset();
            }

            if (pending_entry_.has_value()) {
                std::string failure_message;
                std::string close_error_message;
                if (!close_pending_entry_stream_locked(*pending_entry_, &close_error_message)) {
                    failure_message = std::move(close_error_message);
                } else if (!commit_pending_entry_locked(*pending_entry_, &failure_message)) {
                    // commit helper supplies the failure message
                }
                if (!failure_message.empty()) {
                    discard_pending_entry_locked(*pending_entry_);
                    io_error_ = true;
                    if (io_error_message_.empty()) {
                        io_error_message_ = failure_message;
                    }
                    if (!diagnostic_message_.empty()) {
                        diagnostic_message_ += '\n';
                    }
                    diagnostic_message_ += failure_message;
                    ++error_count_;
                } else {
                    auto append_metadata_warning = [this](std::optional<std::string> const& warning) {
                        if (!warning.has_value()) {
                            return;
                        }
                        if (!diagnostic_message_.empty()) {
                            diagnostic_message_ += '\n';
                        }
                        diagnostic_message_ += *warning;
                        ++error_count_;
                    };
                    append_metadata_warning(apply_item_times(pending_entry_->output_path, pending_entry_->times));
                    append_metadata_warning(
                        apply_item_attributes(pending_entry_->output_path, pending_entry_->attributes));
                    append_metadata_warning(
                        apply_item_security(pending_entry_->archive_index, pending_entry_->output_path));
                    apply_zone_identifier_to_output(pending_entry_->output_path, false);
                    OutputTarget target;
                    target.archive_index = pending_entry_->archive_index;
                    target.archive_entry_path = pending_entry_->archive_entry_path;
                    target.absolute_output_path = pending_entry_->absolute_output_path;
                    target.output_path = pending_entry_->output_path;
                    target.destination_path = pending_entry_->destination_path;
                    target.authorized_root = pending_entry_->authorized_root;
                    target.backup_path = pending_entry_->backup_path;
                    target.transaction = pending_entry_->transaction;
                    target.collided_archive_entry_path = pending_entry_->collided_archive_entry_path;
                    target.original_identity = pending_entry_->original_identity;
                    target.had_original = pending_entry_->had_original;
                    target.overwrote_existing = pending_entry_->overwrote_existing;
                    target.renamed_from_collision = pending_entry_->renamed_from_collision;
                    target.preserve_backup_on_commit = pending_entry_->preserve_backup_on_commit;
                    record_materialized_output_locked(
                        target,
                        pending_entry_->bytes_written,
                        false,
                        pending_entry_->temp_identity,
                        pending_entry_->times);
                }
                pending_entry_.reset();
            }

            if (pending_directory_.has_value()) {
                OutputTarget const& target = pending_directory_->output_target;
                FilesystemObjectIdentity const& expected_identity =
                    pending_directory_->created ? pending_directory_->materialized_identity : target.original_identity;
                std::error_code identity_ec;
                if (!filesystem_object_matches_identity_no_follow(target.output_path, expected_identity, identity_ec)) {
                    if (!diagnostic_message_.empty()) {
                        diagnostic_message_ += '\n';
                    }
                    diagnostic_message_ += "Output directory changed before extraction completed: "
                                         + target.output_path.generic_string()
                                         + (identity_ec ? std::string("; ") + identity_ec.message() : "");
                    ++error_count_;
                    io_error_ = true;
                    if (pending_directory_->budget_file_reserved) {
                        release_budget_file();
                    }
                    pending_directory_.reset();
                }
            }

            if (pending_directory_.has_value()) {
                OutputTarget const& target = pending_directory_->output_target;
                ExtractMaterializedEntry entry;
                entry.archive_entry_path = target.archive_entry_path;
                entry.absolute_output_path = target.absolute_output_path;
                entry.is_directory = true;
                entry.overwrote_existing = target.overwrote_existing;
                entry.renamed_from_collision = target.renamed_from_collision;
                materialized_entries_.push_back(std::move(entry));
                FilesystemObjectIdentity const& materialized_identity =
                    pending_directory_->created ? pending_directory_->materialized_identity : target.original_identity;
                remember_materialized_output_locked(target.archive_index,
                                                    target.output_path,
                                                    target.authorized_root,
                                                    materialized_identity,
                                                    pending_directory_->times,
                                                    true);
                if (pending_directory_->created
                    || target.had_original
                    || pending_directory_->original_metadata_defined) {
                    ExtractRollbackEntry rollback_entry;
                    rollback_entry.output_path = target.output_path;
                    rollback_entry.destination_path = target.destination_path;
                    rollback_entry.backup_path = target.backup_path;
                    rollback_entry.transaction = target.transaction;
                    rollback_entry.had_original = target.had_original;
                    rollback_entry.preserve_backup_on_commit = target.preserve_backup_on_commit;
                    rollback_entry.is_directory = true;
                    rollback_entry.remove_only_if_empty = true;
                    rollback_entry.restore_directory_metadata = pending_directory_->original_metadata_defined;
                    rollback_entry.original_permissions = pending_directory_->original_permissions;
                    rollback_entry.original_mtime = pending_directory_->original_mtime;
                    rollback_entry.original_mtime_defined = pending_directory_->original_mtime_defined;
                    rollback_entry.output_identity = pending_directory_->created
                                                       ? pending_directory_->materialized_identity
                                                       : target.original_identity;
                    if (target.had_original && !target.backup_path.empty()) {
                        rollback_entry.backup_identity = target.original_identity;
                    }
                    rollback_entries_.push_back(std::move(rollback_entry));
                }
                DeferredDirectoryMetadata metadata;
                metadata.archive_index = target.archive_index;
                metadata.output_path = target.output_path;
                metadata.attributes = pending_directory_->attributes;
                metadata.times = pending_directory_->times;
                deferred_directory_metadata_.push_back(std::move(metadata));
                if (target.transaction != nullptr && target.backup_path.empty()) {
                    std::string finish_diagnostic;
                    if (!target.transaction->finish(&finish_diagnostic)) {
                        if (!diagnostic_message_.empty()) {
                            diagnostic_message_ += '\n';
                        }
                        diagnostic_message_ += finish_diagnostic;
                        ++error_count_;
                    }
                }
                pending_directory_.reset();
            }
        }

        std::vector<DeferredHardLink> deferred_links;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deferred_links = std::move(deferred_hard_links_);
            deferred_hard_links_.clear();
            pending_deferred_hard_link_output_.reset();
        }

        auto finalize_deferred_link = [&](DeferredHardLink const& deferred) {
            if (std::optional<std::string> const warning =
                    apply_item_times(deferred.output_target.output_path, deferred.times);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
            if (std::optional<std::string> const warning =
                    apply_item_attributes(deferred.output_target.output_path, deferred.attributes);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
            if (std::optional<std::string> const warning =
                    apply_item_security(deferred.output_target.archive_index, deferred.output_target.output_path);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
            apply_zone_identifier_to_output(deferred.output_target.output_path, false);
            std::error_code identity_ec;
            FilesystemObjectIdentity const identity =
                capture_filesystem_object_identity_no_follow(deferred.output_target.output_path, identity_ec);
            if (!identity_ec && identity.defined) {
                std::lock_guard<std::mutex> lock(mutex_);
                remember_materialized_output_locked(deferred.output_target.archive_index,
                                                    deferred.output_target.output_path,
                                                    deferred.output_target.authorized_root,
                                                    identity,
                                                    deferred.times,
                                                    false);
            }
        };

        while (!deferred_links.empty()) {
            bool made_progress = false;
            std::vector<DeferredHardLink> unresolved;
            unresolved.reserve(deferred_links.size());
            for (DeferredHardLink& deferred : deferred_links) {
                std::string target_warning;
                HardLinkTargetState const state = inspect_hard_link_target(deferred.target_path, target_warning);
                if (state == HardLinkTargetState::kMissing) {
                    unresolved.push_back(std::move(deferred));
                    continue;
                }
                made_progress = true;
                if (state == HardLinkTargetState::kInvalid) {
                    release_budget_file();
                    record_partial_warning(target_warning);
                    continue;
                }

                LinkCreationPlan plan;
                plan.type = ExtractItemLinkInfo::Type::kHardLink;
                plan.hardlink_target_path = deferred.target_path;
                if (std::optional<std::string> const warning =
                        materialize_link(deferred.output_target, plan, false);
                    warning.has_value()) {
                    release_budget_file();
                    record_partial_warning(*warning);
                } else {
                    finalize_deferred_link(deferred);
                }
            }
            deferred_links = std::move(unresolved);
            if (!made_progress) {
                std::unordered_set<std::string> pending_outputs;
                for (DeferredHardLink const& deferred : deferred_links) {
                    pending_outputs.insert(deferred.output_target.output_path.lexically_normal().generic_string());
                }
                for (DeferredHardLink const& deferred : deferred_links) {
                    bool const cycle = pending_outputs.contains(deferred.target_path.lexically_normal().generic_string());
                    release_budget_file();
                    record_partial_warning(std::string("Hard link ")
                                           + (cycle ? "dependency cycle" : "target is missing")
                                           + " and was skipped: "
                                           + deferred.output_target.archive_entry_path
                                           + " -> "
                                           + deferred.target_path.generic_string());
                }
                deferred_links.clear();
            }
        }

        // Transactions without an overwrite backup no longer carry rollback
        // state: rollback can safely create a fresh transaction around the
        // materialized object. Finish these while archive directories are still
        // writable. Otherwise their last shared owner can be destroyed only
        // after restrictive directory modes (for example 0555) are restored,
        // leaving an empty .z7-transaction-* directory behind.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::unordered_set<FilesystemTransaction*> finished_transactions;
            for (ExtractRollbackEntry& entry : rollback_entries_) {
                if (entry.transaction == nullptr || entry.had_original || !entry.backup_path.empty()) {
                    continue;
                }
                FilesystemTransaction* const transaction = entry.transaction.get();
                if (finished_transactions.insert(transaction).second) {
                    std::string finish_diagnostic;
                    if (!transaction->finish(&finish_diagnostic)) {
                        if (!diagnostic_message_.empty()) {
                            diagnostic_message_ += '\n';
                        }
                        diagnostic_message_ += finish_diagnostic;
                        ++error_count_;
                        io_error_ = true;
                        if (io_error_message_.empty()) {
                            io_error_message_ = finish_diagnostic;
                        }
                        continue;
                    }
                }
                entry.transaction.reset();
            }
        }

        std::vector<DeferredDirectoryMetadata> directory_metadata;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            directory_metadata = std::move(deferred_directory_metadata_);
            deferred_directory_metadata_.clear();
        }
        auto const path_depth = [](fs::path const& path) {
            return static_cast<size_t>(std::distance(path.begin(), path.end()));
        };
        std::stable_sort(directory_metadata.begin(), directory_metadata.end(), [&](auto const& lhs, auto const& rhs) {
            return path_depth(lhs.output_path) > path_depth(rhs.output_path);
        });
        for (DeferredDirectoryMetadata const& metadata : directory_metadata) {
            if (std::optional<std::string> const warning = apply_item_times(metadata.output_path, metadata.times);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
            if (std::optional<std::string> const warning =
                    apply_item_attributes(metadata.output_path, metadata.attributes, true);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
            if (std::optional<std::string> const warning =
                    apply_item_security(metadata.archive_index, metadata.output_path);
                warning.has_value()) {
                record_partial_warning(*warning);
            }
        }
    }

    void NativeExtractCallback::apply_zone_identifier_to_output(fs::path const& output_path, bool is_directory) const {
#if defined(_WIN32) && !defined(UNDER_CE)
        if (is_directory) {
            return;
        }
        if (zone_id_mode_ == ExtractZoneIdMode::kNone || archive_metadata_source_path_.empty()) {
            return;
        }
        if (zone_id_mode_ == ExtractZoneIdMode::kOffice && !is_office_document_path(output_path)) {
            return;
        }

        std::string const zone_data = read_zone_identifier_stream(fs::path(archive_metadata_source_path_));
        write_zone_identifier_stream(output_path, zone_data);
#elif defined(__APPLE__)
        if (zone_id_mode_ == ExtractZoneIdMode::kNone || archive_metadata_source_path_.empty()) {
            return;
        }
        if (is_directory && zone_id_mode_ != ExtractZoneIdMode::kAll) {
            return;
        }
        if (!is_directory && zone_id_mode_ == ExtractZoneIdMode::kOffice && !is_office_document_path(output_path)) {
            return;
        }

        std::string const quarantine_data = read_quarantine_xattr(fs::path(archive_metadata_source_path_));
        write_quarantine_xattr(output_path, quarantine_data);
#else
        (void)output_path;
        (void)is_directory;
#endif
    }

    STDMETHODIMP NativeExtractCallback::SetOperationResult(Int32 op_res) throw() {
        std::optional<PendingDataSymlink> data_symlink;
        std::optional<PendingAlternateStream> alternate_stream;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_data_symlink_.has_value()) {
                data_symlink = std::move(pending_data_symlink_);
                pending_data_symlink_.reset();
            }
            if (pending_alternate_stream_.has_value()) {
                alternate_stream = std::move(pending_alternate_stream_);
                pending_alternate_stream_.reset();
            }
        }

        if (alternate_stream.has_value()) {
            bool committed = false;
            std::optional<std::string> warning;
            if (op_res == NArchive::NExtract::NOperationResult::kOK) {
                std::string close_error;
                if (!close_pending_alternate_stream(*alternate_stream, &close_error)) {
                    warning = std::move(close_error);
                } else {
                    warning = commit_pending_alternate_stream(*alternate_stream);
                    committed = !warning.has_value();
                    if (committed) {
                        if (std::optional<std::string> const time_warning =
                                apply_item_times(alternate_stream->output_path,
                                                 alternate_stream->parent_times,
                                                 alternate_stream->parent_is_symlink);
                            time_warning.has_value()) {
                            record_partial_warning(*time_warning);
                        }
                    }
                }
            }
            if (warning.has_value()) {
                record_partial_warning(*warning);
            }
            if (std::optional<std::string> const cleanup =
                    discard_pending_alternate_stream(*alternate_stream, !committed);
                cleanup.has_value()) {
                record_partial_warning(*cleanup);
            }
        }

        if (data_symlink.has_value()) {
            auto release_reservations = [&]() {
                if (budget_tracker_ != nullptr && data_symlink->budget_bytes_reserved != 0) {
                    budget_tracker_->release_bytes(data_symlink->budget_bytes_reserved);
                    data_symlink->budget_bytes_reserved = 0;
                }
                release_budget_file();
            };
            auto finish_unused_transaction = [&]() {
                if (data_symlink->output_target.transaction == nullptr) {
                    return;
                }
                std::string finish_diagnostic;
                if (!data_symlink->output_target.transaction->finish(&finish_diagnostic)) {
                    record_io_error(finish_diagnostic);
                }
            };

            if (op_res != NArchive::NExtract::NOperationResult::kOK) {
                release_reservations();
                finish_unused_transaction();
            } else {
                uint64_t const actual_size = static_cast<uint64_t>(data_symlink->target_data.size());
                bool budget_ok = true;
                if (budget_tracker_ != nullptr && actual_size != data_symlink->budget_bytes_reserved) {
                    budget_tracker_->release_bytes(data_symlink->budget_bytes_reserved);
                    data_symlink->budget_bytes_reserved = 0;
                    budget_ok = budget_tracker_->try_reserve_bytes(actual_size);
                    if (budget_ok) {
                        data_symlink->budget_bytes_reserved = actual_size;
                    }
                }

                FilesystemObjectIdentity materialized_identity;
                std::optional<std::string> warning;
                if (!budget_ok) {
                    warning = "Symbolic link target exceeded the extraction byte budget: "
                            + data_symlink->output_target.archive_entry_path;
                } else {
                    warning = materialize_data_symlink(*data_symlink, &materialized_identity);
                }
                if (warning.has_value()) {
                    release_reservations();
                    finish_unused_transaction();
                    record_partial_warning(*warning);
                } else {
                    if (std::optional<std::string> const metadata_warning = apply_item_times(
                            data_symlink->output_target.output_path, data_symlink->times, true);
                        metadata_warning.has_value()) {
                        record_partial_warning(*metadata_warning);
                    }
                    if (std::optional<std::string> const security_warning = apply_item_security(
                            data_symlink->output_target.archive_index, data_symlink->output_target.output_path);
                        security_warning.has_value()) {
                        record_partial_warning(*security_warning);
                    }
                    apply_zone_identifier_to_output(data_symlink->output_target.output_path, false);
                    std::lock_guard<std::mutex> lock(mutex_);
                    remember_materialized_output_locked(data_symlink->output_target.archive_index,
                                                        data_symlink->output_target.output_path,
                                                        data_symlink->output_target.authorized_root,
                                                        materialized_identity,
                                                        data_symlink->times,
                                                        false,
                                                        true);
                }
            }
        }

        std::string path;
        std::string diagnostic;
        bool force_hresult_failure = false;
        bool encrypted_item = false;
        std::optional<fs::path> zone_identifier_target;
        std::optional<std::string> attribute_warning;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto append_diagnostic_locked = [this](std::string const& message) {
                if (message.empty()) {
                    return;
                }
                if (!diagnostic_message_.empty()) {
                    diagnostic_message_ += '\n';
                }
                diagnostic_message_ += message;
            };
            path = current_path_;
            encrypted_item = current_item_encrypted_;
            ++completed_files_;
            if (op_res == NArchive::NExtract::NOperationResult::kOK) {
                pending_deferred_hard_link_output_.reset();
                std::string failure_message;
                if (pending_link_.has_value()) {
                    PendingLink const& pending = *pending_link_;
                    std::error_code identity_ec;
                    if (!filesystem_object_matches_identity_no_follow(
                            pending.output_target.output_path, pending.materialized_identity, identity_ec)) {
                        failure_message = "Created output link changed before extraction completed: "
                                        + pending.output_target.output_path.generic_string()
                                        + (identity_ec ? std::string("; ") + identity_ec.message() : "");
                        if (!materialized_entries_.empty()
                            && materialized_entries_.back().absolute_output_path
                                   == pending.output_target.absolute_output_path) {
                            materialized_entries_.pop_back();
                        }
                        if (!rollback_entries_.empty()
                            && rollback_entries_.back().output_path == pending.output_target.output_path) {
                            rollback_entries_.pop_back();
                        }
                        materialized_output_targets_.erase(pending.output_target.archive_index);
                        release_budget_file();
                    } else {
                        if (std::optional<std::string> const warning =
                                apply_item_times(pending.output_target.output_path, pending.times, pending.is_symlink);
                            warning.has_value()) {
                            append_diagnostic_locked(*warning);
                            ++error_count_;
                        }
                        if (!pending.is_symlink) {
                            if (std::optional<std::string> const warning =
                                    apply_item_attributes(pending.output_target.output_path, pending.attributes);
                                warning.has_value()) {
                                append_diagnostic_locked(*warning);
                                ++error_count_;
                            }
                        }
                        if (std::optional<std::string> const warning = apply_item_security(
                                pending.output_target.archive_index, pending.output_target.output_path);
                            warning.has_value()) {
                            append_diagnostic_locked(*warning);
                            ++error_count_;
                        }
                        zone_identifier_target = pending.output_target.output_path;
                        remember_materialized_output_locked(pending.output_target.archive_index,
                                                            pending.output_target.output_path,
                                                            pending.output_target.authorized_root,
                                                            pending.materialized_identity,
                                                            pending.times,
                                                            false,
                                                            pending.is_symlink);
                    }
                    pending_link_.reset();
                }
                if (failure_message.empty() && pending_directory_.has_value()) {
                    OutputTarget const& target = pending_directory_->output_target;
                    FilesystemObjectIdentity const& expected_identity = pending_directory_->created
                                                                          ? pending_directory_->materialized_identity
                                                                          : target.original_identity;
                    std::error_code identity_ec;
                    if (!filesystem_object_matches_identity_no_follow(
                            target.output_path, expected_identity, identity_ec)) {
                        failure_message = "Output directory changed before extraction completed: "
                                        + target.output_path.generic_string()
                                        + (identity_ec ? std::string("; ") + identity_ec.message() : "");
                        if (pending_directory_->budget_file_reserved) {
                            release_budget_file();
                        }
                        pending_directory_.reset();
                    }
                }
                if (failure_message.empty() && pending_directory_.has_value()) {
                    OutputTarget& target = pending_directory_->output_target;
                    std::error_code canonical_ec;
                    fs::path const canonical_path = fs::weakly_canonical(target.output_path, canonical_ec);
                    if (!canonical_ec) {
                        target.absolute_output_path = canonical_path.generic_string();
                    }
                    ExtractMaterializedEntry entry;
                    entry.archive_entry_path = target.archive_entry_path;
                    entry.absolute_output_path = target.absolute_output_path;
                    entry.is_directory = true;
                    entry.overwrote_existing = target.overwrote_existing;
                    entry.renamed_from_collision = target.renamed_from_collision;
                    materialized_entries_.push_back(std::move(entry));
                    FilesystemObjectIdentity const& materialized_identity =
                        pending_directory_->created ? pending_directory_->materialized_identity : target.original_identity;
                    remember_materialized_output_locked(target.archive_index,
                                                        target.output_path,
                                                        target.authorized_root,
                                                        materialized_identity,
                                                        pending_directory_->times,
                                                        true);

                    if (pending_directory_->created
                        || target.had_original
                        || pending_directory_->original_metadata_defined) {
                        ExtractRollbackEntry rollback_entry;
                        rollback_entry.output_path = target.output_path;
                        rollback_entry.destination_path = target.destination_path;
                        rollback_entry.backup_path = target.backup_path;
                        rollback_entry.transaction = target.transaction;
                        rollback_entry.had_original = target.had_original;
                        rollback_entry.preserve_backup_on_commit = target.preserve_backup_on_commit;
                        rollback_entry.is_directory = true;
                        rollback_entry.remove_only_if_empty = true;
                        rollback_entry.restore_directory_metadata = pending_directory_->original_metadata_defined;
                        rollback_entry.original_permissions = pending_directory_->original_permissions;
                        rollback_entry.original_mtime = pending_directory_->original_mtime;
                        rollback_entry.original_mtime_defined = pending_directory_->original_mtime_defined;
                        rollback_entry.output_identity = pending_directory_->created
                                                           ? pending_directory_->materialized_identity
                                                           : target.original_identity;
                        if (target.had_original && !target.backup_path.empty()) {
                            rollback_entry.backup_identity = target.original_identity;
                        }
                        rollback_entries_.push_back(std::move(rollback_entry));
                    }

                    DeferredDirectoryMetadata metadata;
                    metadata.archive_index = target.archive_index;
                    metadata.output_path = target.output_path;
                    metadata.attributes = pending_directory_->attributes;
                    metadata.times = pending_directory_->times;
                    deferred_directory_metadata_.push_back(std::move(metadata));
                    pending_directory_.reset();
                }
                if (failure_message.empty() && pending_entry_.has_value()) {
                    std::string close_error_message;
                    if (!close_pending_entry_stream_locked(*pending_entry_, &close_error_message)) {
                        failure_message = std::move(close_error_message);
                    } else if (!commit_pending_entry_locked(*pending_entry_, &failure_message)) {
                        // commit helper supplies the concrete failure message
                    } else {
                        if (std::optional<std::string> const warning =
                                apply_item_times(pending_entry_->output_path, pending_entry_->times);
                            warning.has_value()) {
                            append_diagnostic_locked(*warning);
                            ++error_count_;
                            attribute_warning = warning;
                        }
                        if (std::optional<std::string> const warning =
                                apply_item_attributes(pending_entry_->output_path, pending_entry_->attributes);
                            warning.has_value()) {
                            append_diagnostic_locked(*warning);
                            ++error_count_;
                            if (attribute_warning.has_value()) {
                                *attribute_warning += '\n' + *warning;
                            } else {
                                attribute_warning = warning;
                            }
                        }
                        if (std::optional<std::string> const warning =
                                apply_item_security(pending_entry_->archive_index, pending_entry_->output_path);
                            warning.has_value()) {
                            append_diagnostic_locked(*warning);
                            ++error_count_;
                            if (attribute_warning.has_value()) {
                                *attribute_warning += '\n' + *warning;
                            } else {
                                attribute_warning = warning;
                            }
                        }
                        zone_identifier_target = pending_entry_->output_path;
                    }
                }

                if (!failure_message.empty()) {
                    if (pending_entry_.has_value()) {
                        discard_pending_entry_locked(*pending_entry_);
                    }
                    io_error_ = true;
                    if (io_error_message_.empty()) {
                        io_error_message_ = failure_message;
                    }
                    append_diagnostic_locked(failure_message);
                    diagnostic = std::move(failure_message);
                    ++error_count_;
                    force_hresult_failure = true;
                } else if (pending_entry_.has_value()) {
                    std::error_code canonical_ec;
                    fs::path const canonical_path = fs::weakly_canonical(pending_entry_->output_path, canonical_ec);
                    if (!canonical_ec) {
                        pending_entry_->absolute_output_path = canonical_path.generic_string();
                    }
                    ExtractMaterializedEntry me;
                    me.archive_entry_path = std::move(pending_entry_->archive_entry_path);
                    me.absolute_output_path = std::move(pending_entry_->absolute_output_path);
                    me.is_directory = false;
                    me.bytes_written = pending_entry_->bytes_written;
                    me.overwrote_existing = pending_entry_->overwrote_existing;
                    me.renamed_from_collision = pending_entry_->renamed_from_collision;
                    materialized_entries_.push_back(std::move(me));
                    remember_materialized_output_locked(pending_entry_->archive_index,
                                                        pending_entry_->output_path,
                                                        pending_entry_->authorized_root,
                                                        pending_entry_->temp_identity,
                                                        pending_entry_->times,
                                                        false);

                    ExtractRollbackEntry rollback_entry;
                    rollback_entry.output_path = pending_entry_->output_path;
                    rollback_entry.destination_path = pending_entry_->destination_path;
                    rollback_entry.backup_path = pending_entry_->backup_path;
                    rollback_entry.transaction = pending_entry_->transaction;
                    rollback_entry.had_original = pending_entry_->had_original;
                    rollback_entry.preserve_backup_on_commit = pending_entry_->preserve_backup_on_commit;
                    rollback_entry.is_directory = false;
                    rollback_entry.output_identity = pending_entry_->temp_identity;
                    if (pending_entry_->had_original && !pending_entry_->backup_path.empty()) {
                        rollback_entry.backup_identity = pending_entry_->original_identity;
                    }
                    rollback_entries_.push_back(std::move(rollback_entry));
                }
                pending_entry_.reset();
            } else {
                if (pending_deferred_hard_link_output_.has_value()) {
                    auto const deferred_it = std::find_if(deferred_hard_links_.begin(),
                                                          deferred_hard_links_.end(),
                                                          [&](DeferredHardLink const& deferred) {
                                                              return deferred.output_target.output_path
                                                                  == *pending_deferred_hard_link_output_;
                                                          });
                    if (deferred_it != deferred_hard_links_.end()) {
                        deferred_hard_links_.erase(deferred_it);
                        release_budget_file();
                    }
                    pending_deferred_hard_link_output_.reset();
                }
                if (pending_link_.has_value()) {
                    OutputTarget const target = pending_link_->output_target;
                    std::string cleanup_error;
                    if (!cleanup_materialized_target_locked(
                            target, pending_link_->materialized_identity, &cleanup_error)) {
                        append_diagnostic_locked(cleanup_error);
                    }
                    if (!materialized_entries_.empty()
                        && materialized_entries_.back().absolute_output_path == target.absolute_output_path) {
                        materialized_entries_.pop_back();
                    }
                    if (!rollback_entries_.empty() && rollback_entries_.back().output_path == target.output_path) {
                        rollback_entries_.pop_back();
                    }
                    materialized_output_targets_.erase(target.archive_index);
                    release_budget_file();
                    pending_link_.reset();
                }
                if (pending_directory_.has_value()) {
                    OutputTarget const& target = pending_directory_->output_target;
                    if (pending_directory_->created) {
                        std::string cleanup_error;
                        if (!cleanup_materialized_target_locked(
                                target, pending_directory_->materialized_identity, &cleanup_error)) {
                            append_diagnostic_locked(cleanup_error);
                        }
                    }
                    if (pending_directory_->budget_file_reserved) {
                        release_budget_file();
                    }
                    pending_directory_.reset();
                }
                std::string close_error_message;
                if (pending_entry_.has_value()
                    && !close_pending_entry_stream_locked(*pending_entry_, &close_error_message)) {
                    io_error_ = true;
                    if (io_error_message_.empty()) {
                        io_error_message_ = close_error_message;
                    }
                    append_diagnostic_locked(close_error_message);
                }
                if (pending_entry_.has_value()) {
                    discard_pending_entry_locked(*pending_entry_);
                }
                pending_entry_.reset();
                ++error_count_;
                if (op_res == NArchive::NExtract::NOperationResult::kWrongPassword
                    || (encrypted_item
                        && (op_res == NArchive::NExtract::NOperationResult::kDataError
                            || op_res == NArchive::NExtract::NOperationResult::kCRCError
                            || op_res == NArchive::NExtract::NOperationResult::kHeadersError))) {
                    password_retry_required_ = true;
                    wrong_password_ = true;
                }
                diagnostic = format_operation_result_message(op_res, encrypted_item, path);
                if (!diagnostic.empty()) {
                    append_diagnostic_locked(diagnostic);
                }
            }
        }

        if (zone_identifier_target.has_value()) {
            apply_zone_identifier_to_output(*zone_identifier_target, false);
        }
        if (attribute_warning.has_value()) {
            emit_log_event(hooks_, OperationStage::kRunning, OutputChannel::kStdErr, *attribute_warning);
        }

        if (op_res != NArchive::NExtract::NOperationResult::kOK || force_hresult_failure) {
            std::string message = std::move(diagnostic);
            if (message.empty()) {
                message = format_operation_result_message(op_res, encrypted_item, path);
            }
            emit_archive_scoped_error(hooks_, archive_path_, archive_error_path_reported_, message);
        }

        emit_progress_snapshot();
        if (force_hresult_failure) {
            return E_FAIL;
        }
        return check_canceled();
    }

} // namespace z7::app
