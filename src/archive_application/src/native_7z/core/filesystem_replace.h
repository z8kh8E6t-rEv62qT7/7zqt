#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "archive_types.h"
#include "core/internal_base.h"

namespace z7::app {

    namespace fs = std::filesystem;

    // Test seam for failures that are otherwise difficult to reproduce. The
    // production backend performs directory-anchored, no-replace moves.
    struct FilesystemTransactionOps {
        std::function<bool(fs::path const&, fs::path const&, std::error_code&)> move_no_replace;
        std::function<bool(fs::path const&, std::error_code&)> create_private_directory;
        std::function<bool(fs::path const&, std::error_code&)> remove_one;
    };

    struct TransactionMoveResult {
        bool success = false;
        bool source_preserved = false;
        bool destination_preserved = false;
        fs::path preserved_path;
        std::string diagnostic;
    };

    // A transaction owns a private directory on the target volume. Objects are
    // never deleted through caller-visible paths: they are first moved into this
    // directory with no-replace semantics and their identity is verified there.
    class FilesystemTransaction final {
    public:
        static std::unique_ptr<FilesystemTransaction> create(fs::path const& volume_anchor,
                                                             std::string_view purpose,
                                                             std::error_code& ec,
                                                             FilesystemTransactionOps const* ops = nullptr);

        ~FilesystemTransaction();
        FilesystemTransaction(FilesystemTransaction const&) = delete;
        FilesystemTransaction& operator=(FilesystemTransaction const&) = delete;

        fs::path const& directory() const { return directory_; }
        fs::path allocate_path(std::string_view role);

        TransactionMoveResult quarantine(fs::path const& source,
                                         FilesystemObjectIdentity const* expected_identity = nullptr);
        TransactionMoveResult restore(fs::path const& stored, fs::path const& destination);
        TransactionMoveResult promote(fs::path const& source, fs::path const& destination);
        TransactionMoveResult discard(fs::path const& stored,
                                      FilesystemObjectIdentity const* expected_identity = nullptr);

        // Removes the transaction directory only when it is empty. A non-empty
        // directory is deliberately retained so recovery remains possible.
        bool finish(std::string* diagnostic = nullptr);

    private:
        FilesystemTransaction(fs::path directory, FilesystemTransactionOps const* ops);

        bool move_no_replace(fs::path const& source, fs::path const& destination, std::error_code& ec) const;
        bool remove_one(fs::path const& path, std::error_code& ec) const;
        bool owns(fs::path const& path) const;

        fs::path directory_;
        FilesystemTransactionOps const* ops_ = nullptr;
        uint64_t next_name_ = 0;
        bool finished_ = false;
    };

    struct AtomicReplaceOptions {
        bool preserve_backup_on_success = false;
        FilesystemObjectIdentity const* expected_destination_identity = nullptr;
        std::function<bool(fs::path const&, std::string&)> validate_quarantined_destination;
    };

    struct AtomicReplaceResult {
        bool success = false;
        bool destination_existed = false;
        bool stale = false;
        bool original_restored = false;
        bool original_restore_failed = false;
        bool backup_retained = false;
        bool replacement_committed = false;
        bool source_exists = false;
        fs::path source_path;
        fs::path destination_path;
        fs::path backup_path;
        fs::path transaction_directory;
        std::optional<OperationResult> error;
    };

    AtomicReplaceResult replace_file_atomically(fs::path const& source_path,
                                                fs::path const& destination_path,
                                                std::string_view purpose,
                                                FilesystemTransactionOps const* ops = nullptr,
                                                AtomicReplaceOptions const* options = nullptr);

} // namespace z7::app
