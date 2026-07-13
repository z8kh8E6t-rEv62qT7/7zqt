#include "core/filesystem_replace.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include "core/internal_results.h"

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/stdio.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace z7::app {
    namespace {

        std::atomic<uint64_t> g_transaction_sequence{1};

        std::optional<OperationResult> make_io_failure(std::string message) {
            return make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo, std::move(message), 2);
        }

        bool path_exists_no_follow(fs::path const& path, std::error_code& ec) {
            ec.clear();
            fs::file_status const status = fs::symlink_status(path, ec);
            if (ec == std::errc::no_such_file_or_directory) {
                ec.clear();
                return false;
            }
            return !ec && fs::status_known(status) && status.type() != fs::file_type::not_found;
        }

#if !defined(_WIN32)
        class DirectoryFd final {
        public:
            explicit DirectoryFd(fs::path const& path) : fd_(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {}
            ~DirectoryFd() {
                if (fd_ >= 0) {
                    ::close(fd_);
                }
            }
            DirectoryFd(DirectoryFd const&) = delete;
            DirectoryFd& operator=(DirectoryFd const&) = delete;
            int get() const { return fd_; }

        private:
            int fd_ = -1;
        };
#endif

        bool default_move_no_replace(fs::path const& source,
                                     fs::path const& destination,
                                     std::error_code& ec) {
            ec.clear();
            if (source.filename().empty() || destination.filename().empty()) {
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }
#if defined(_WIN32)
            HANDLE const source_handle = ::CreateFileW(source.c_str(),
                                                       DELETE | FILE_READ_ATTRIBUTES,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                       nullptr,
                                                       OPEN_EXISTING,
                                                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                                       nullptr);
            if (source_handle == INVALID_HANDLE_VALUE) {
                ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
                return false;
            }
            HANDLE const destination_parent = ::CreateFileW(destination.parent_path().c_str(),
                                                            FILE_LIST_DIRECTORY | DELETE | SYNCHRONIZE,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            nullptr,
                                                            OPEN_EXISTING,
                                                            FILE_FLAG_BACKUP_SEMANTICS,
                                                            nullptr);
            if (destination_parent == INVALID_HANDLE_VALUE) {
                int const error = static_cast<int>(::GetLastError());
                ::CloseHandle(source_handle);
                ec = std::error_code(error, std::system_category());
                return false;
            }
            std::wstring const destination_name = destination.filename().wstring();
            size_t const name_bytes = destination_name.size() * sizeof(wchar_t);
            std::vector<std::byte> storage(sizeof(FILE_RENAME_INFO) + name_bytes);
            auto* rename_info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            rename_info->ReplaceIfExists = FALSE;
            rename_info->RootDirectory = destination_parent;
            rename_info->FileNameLength = static_cast<DWORD>(name_bytes);
            std::memcpy(rename_info->FileName, destination_name.data(), name_bytes);
            bool const renamed = ::SetFileInformationByHandle(
                source_handle, FileRenameInfo, rename_info, static_cast<DWORD>(storage.size()));
            int const error = renamed ? ERROR_SUCCESS : static_cast<int>(::GetLastError());
            ::CloseHandle(destination_parent);
            ::CloseHandle(source_handle);
            if (renamed) {
                return true;
            }
            ec = std::error_code(error, std::system_category());
            return false;
#else
            DirectoryFd source_parent(source.parent_path());
            DirectoryFd destination_parent(destination.parent_path());
            if (source_parent.get() < 0 || destination_parent.get() < 0) {
                ec = std::error_code(errno, std::generic_category());
                return false;
            }
#if defined(__APPLE__)
            unsigned int const flags = RENAME_EXCL | RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH;
            if (::renameatx_np(source_parent.get(),
                               source.filename().c_str(),
                               destination_parent.get(),
                               destination.filename().c_str(),
                               flags)
                == 0) {
                return true;
            }
            ec = std::error_code(errno, std::generic_category());
            return false;
#elif defined(SYS_renameat2)
            constexpr unsigned int kRenameNoReplace = 1;
            if (::syscall(SYS_renameat2,
                          source_parent.get(),
                          source.filename().c_str(),
                          destination_parent.get(),
                          destination.filename().c_str(),
                          kRenameNoReplace)
                == 0) {
                return true;
            }
            ec = std::error_code(errno, std::generic_category());
            return false;
#else
            ec = std::make_error_code(std::errc::not_supported);
            return false;
#endif
#endif
        }

        bool default_remove_one(fs::path const& path, std::error_code& ec) {
            ec.clear();
            bool const removed = fs::remove(path, ec);
            return removed && !ec;
        }

        std::string preserved_message(fs::path const& path) {
            return "temporary file preserved at " + path.string();
        }

        bool same_identity(FilesystemObjectIdentity const& lhs, FilesystemObjectIdentity const& rhs) {
            return lhs.defined && rhs.defined && lhs.volume == rhs.volume && lhs.object == rhs.object;
        }

    } // namespace

    FilesystemTransaction::FilesystemTransaction(fs::path directory, FilesystemTransactionOps const* ops) :
        directory_(std::move(directory)), ops_(ops) {}

    std::unique_ptr<FilesystemTransaction> FilesystemTransaction::create(fs::path const& volume_anchor,
                                                                         std::string_view purpose,
                                                                         std::error_code& ec,
                                                                         FilesystemTransactionOps const* ops) {
        ec.clear();
        fs::path parent = volume_anchor.parent_path();
        if (parent.empty()) {
            ec = std::make_error_code(std::errc::invalid_argument);
            return nullptr;
        }

        std::string clean_purpose;
        clean_purpose.reserve(purpose.size());
        for (char ch : purpose) {
            clean_purpose.push_back((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                                        ? ch
                                        : '-');
        }
        if (clean_purpose.empty()) {
            clean_purpose = "operation";
        }

        for (uint32_t attempt = 0; attempt < 128; ++attempt) {
            uint64_t const sequence = g_transaction_sequence.fetch_add(1, std::memory_order_relaxed);
            fs::path const candidate =
                parent / fs::path(".z7-transaction-" + clean_purpose + "-" + std::to_string(sequence));
            std::error_code create_ec;
            bool const created = ops != nullptr && ops->create_private_directory
                                   ? ops->create_private_directory(candidate, create_ec)
                                   : create_private_directory(candidate, create_ec);
            if (created) {
                return std::unique_ptr<FilesystemTransaction>(new FilesystemTransaction(candidate, ops));
            }
            if (create_ec != std::errc::file_exists) {
                ec = create_ec;
                return nullptr;
            }
        }
        ec = std::make_error_code(std::errc::file_exists);
        return nullptr;
    }

    FilesystemTransaction::~FilesystemTransaction() {
        if (!finished_) {
            std::string ignored;
            (void)finish(&ignored);
        }
    }

    fs::path FilesystemTransaction::allocate_path(std::string_view role) {
        std::string clean_role;
        clean_role.reserve(role.size());
        for (char ch : role) {
            clean_role.push_back((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                                     ? ch
                                     : '-');
        }
        if (clean_role.empty()) {
            clean_role = "object";
        }
        return directory_ / fs::path(clean_role + "-" + std::to_string(++next_name_));
    }

    bool FilesystemTransaction::move_no_replace(fs::path const& source,
                                                fs::path const& destination,
                                                std::error_code& ec) const {
        if (ops_ != nullptr && ops_->move_no_replace) {
            return ops_->move_no_replace(source, destination, ec);
        }
        return default_move_no_replace(source, destination, ec);
    }

    bool FilesystemTransaction::remove_one(fs::path const& path, std::error_code& ec) const {
        if (ops_ != nullptr && ops_->remove_one) {
            return ops_->remove_one(path, ec);
        }
        return default_remove_one(path, ec);
    }

    bool FilesystemTransaction::owns(fs::path const& path) const {
        return path.parent_path().lexically_normal() == directory_.lexically_normal();
    }

    TransactionMoveResult FilesystemTransaction::quarantine(
        fs::path const& source, FilesystemObjectIdentity const* expected_identity) {
        TransactionMoveResult result;
        fs::path const stored = allocate_path("backup");
        std::error_code ec;
        if (!move_no_replace(source, stored, ec)) {
            result.source_preserved = path_exists_no_follow(source, ec);
            result.diagnostic = "failed to move object into private transaction: " + ec.message();
            return result;
        }

        result.preserved_path = stored;
        FilesystemObjectIdentity const moved_identity = capture_filesystem_object_identity_no_follow(stored, ec);
        if (ec || !moved_identity.defined
            || (expected_identity != nullptr && !same_identity(moved_identity, *expected_identity))) {
            std::error_code restore_ec;
            if (move_no_replace(stored, source, restore_ec)) {
                result.source_preserved = true;
                result.preserved_path.clear();
                result.diagnostic = expected_identity != nullptr
                                      ? "filesystem object identity changed before transaction"
                                      : "failed to identify quarantined filesystem object";
            } else {
                result.preserved_path = stored;
                result.diagnostic = "filesystem object identity changed before transaction; "
                                  + preserved_message(stored) + "; restore failed: " + restore_ec.message();
            }
            return result;
        }
        result.success = true;
        result.source_preserved = true;
        return result;
    }

    TransactionMoveResult FilesystemTransaction::restore(fs::path const& stored, fs::path const& destination) {
        TransactionMoveResult result;
        result.preserved_path = stored;
        if (!owns(stored)) {
            result.diagnostic = "transaction refused to restore an unowned path";
            return result;
        }
        std::error_code ec;
        if (!move_no_replace(stored, destination, ec)) {
            result.source_preserved = path_exists_no_follow(stored, ec);
            result.destination_preserved = path_exists_no_follow(destination, ec);
            result.diagnostic = "rollback incomplete: destination was occupied or restore failed; "
                              + preserved_message(stored) + "; " + ec.message();
            return result;
        }
        result.success = true;
        result.destination_preserved = true;
        result.preserved_path.clear();
        return result;
    }

    TransactionMoveResult FilesystemTransaction::promote(fs::path const& source, fs::path const& destination) {
        TransactionMoveResult result;
        std::error_code ec;
        if (!move_no_replace(source, destination, ec)) {
            result.source_preserved = path_exists_no_follow(source, ec);
            result.destination_preserved = path_exists_no_follow(destination, ec);
            result.preserved_path = source;
            result.diagnostic = "failed to commit transaction: " + ec.message();
            if (result.source_preserved) {
                result.diagnostic += "; " + preserved_message(source);
            }
            return result;
        }
        result.success = true;
        result.destination_preserved = true;
        return result;
    }

    TransactionMoveResult FilesystemTransaction::discard(
        fs::path const& stored, FilesystemObjectIdentity const* expected_identity) {
        TransactionMoveResult result;
        result.preserved_path = stored;
        if (!owns(stored)) {
            result.diagnostic = "transaction refused to delete an unowned path";
            return result;
        }
        std::error_code ec;
        FilesystemObjectIdentity const identity = capture_filesystem_object_identity_no_follow(stored, ec);
        if (ec || !identity.defined
            || (expected_identity != nullptr && !same_identity(identity, *expected_identity))) {
            result.source_preserved = !ec;
            result.diagnostic = "transaction object identity changed; " + preserved_message(stored);
            return result;
        }
        if (!remove_one(stored, ec)) {
            result.source_preserved = true;
            result.diagnostic = "failed to remove transaction object; " + preserved_message(stored) + "; "
                              + ec.message();
            return result;
        }
        result.success = true;
        result.preserved_path.clear();
        return result;
    }

    bool FilesystemTransaction::finish(std::string* diagnostic) {
        if (finished_) {
            return true;
        }
        std::error_code ec;
        bool const removed = remove_one(directory_, ec);
        finished_ = removed;
        if (!removed && diagnostic != nullptr) {
            *diagnostic = "temporary file preserved in transaction directory " + directory_.string();
            if (ec) {
                *diagnostic += "; " + ec.message();
            }
        }
        return removed;
    }

    AtomicReplaceResult replace_file_atomically(fs::path const& source_path,
                                                fs::path const& destination_path,
                                                std::string_view purpose,
                                                FilesystemTransactionOps const* ops,
                                                AtomicReplaceOptions const* options) {
        AtomicReplaceResult result;
        result.source_path = source_path;
        result.destination_path = destination_path;
        AtomicReplaceOptions const resolved_options = options != nullptr ? *options : AtomicReplaceOptions{};

        std::error_code ec;
        result.destination_existed = path_exists_no_follow(destination_path, ec);
        if (ec) {
            result.error = make_io_failure("Failed to inspect replacement destination: " + ec.message());
            return result;
        }

        std::unique_ptr<FilesystemTransaction> transaction =
            FilesystemTransaction::create(destination_path, purpose, ec, ops);
        if (!transaction) {
            result.source_exists = path_exists_no_follow(source_path, ec);
            result.error = make_io_failure("Failed to create private filesystem transaction: " + ec.message()
                                           + (result.source_exists ? "; " + preserved_message(source_path) : ""));
            return result;
        }
        result.transaction_directory = transaction->directory();

        if (result.destination_existed) {
            TransactionMoveResult backup =
                transaction->quarantine(destination_path, resolved_options.expected_destination_identity);
            if (!backup.success) {
                result.stale = resolved_options.expected_destination_identity != nullptr;
                result.source_exists = path_exists_no_follow(source_path, ec);
                result.backup_path = backup.preserved_path;
                result.backup_retained = !backup.preserved_path.empty();
                result.error = make_io_failure((result.stale ? "stale archive source: " : "") + backup.diagnostic);
                return result;
            }
            result.backup_path = backup.preserved_path;
            if (resolved_options.validate_quarantined_destination) {
                std::string validation_error;
                if (!resolved_options.validate_quarantined_destination(result.backup_path, validation_error)) {
                    TransactionMoveResult const restored =
                        transaction->restore(result.backup_path, destination_path);
                    result.stale = true;
                    result.original_restored = restored.success;
                    result.original_restore_failed = !restored.success;
                    result.backup_retained = !restored.success;
                    if (restored.success) {
                        result.backup_path.clear();
                    }
                    result.source_exists = path_exists_no_follow(source_path, ec);
                    result.error = make_io_failure("stale archive source: " + validation_error
                                                   + (restored.success ? "" : "; " + restored.diagnostic));
                    return result;
                }
            }
        } else if (resolved_options.expected_destination_identity != nullptr) {
            result.stale = true;
            result.source_exists = path_exists_no_follow(source_path, ec);
            result.error = make_io_failure("stale archive source: source object disappeared before commit");
            return result;
        }

        TransactionMoveResult promoted = transaction->promote(source_path, destination_path);
        result.source_exists = promoted.source_preserved;
        if (!promoted.success) {
            if (result.destination_existed) {
                TransactionMoveResult restored = transaction->restore(result.backup_path, destination_path);
                result.original_restored = restored.success;
                result.original_restore_failed = !restored.success;
                result.backup_retained = !restored.success;
                if (restored.success) {
                    result.backup_path.clear();
                }
                if (!restored.success) {
                    promoted.diagnostic += "; " + restored.diagnostic;
                }
            }
            result.error = make_io_failure(promoted.diagnostic);
            return result;
        }

        result.replacement_committed = true;
        if (result.destination_existed && !resolved_options.preserve_backup_on_success) {
            FilesystemObjectIdentity const backup_identity =
                capture_filesystem_object_identity_no_follow(result.backup_path, ec);
            TransactionMoveResult discarded = transaction->discard(result.backup_path, &backup_identity);
            if (!discarded.success) {
                result.backup_retained = true;
                result.error = make_io_failure("Archive replacement committed, but backup cleanup failed: "
                                               + discarded.diagnostic);
                return result;
            }
            result.backup_path.clear();
        } else if (result.destination_existed) {
            result.backup_retained = true;
        }

        std::string finish_diagnostic;
        if (!resolved_options.preserve_backup_on_success && !transaction->finish(&finish_diagnostic)) {
            result.error = make_io_failure("Archive replacement committed, but transaction cleanup failed: "
                                           + finish_diagnostic);
            return result;
        }
        result.success = true;
        result.source_exists = false;
        return result;
    }

} // namespace z7::app
