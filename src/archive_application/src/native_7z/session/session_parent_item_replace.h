// src/archive_application/src/native_7z/session/session_parent_item_replace.h
// Role: Exact archive-item replacement used when committing nested sessions.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include "archive_types.h"

namespace z7::app {

    struct ArchiveBackendHooks;
    class ArchiveOpenSession;

    std::optional<OperationResult>
    validate_archive_session_parent_item_replacement(ArchiveOpenSession const& parent);

    OperationResult replace_archive_session_item_by_index(ArchiveOpenSession& parent,
                                                           uint32_t parent_entry_index,
                                                           std::filesystem::path const& replacement_path,
                                                           ArchiveBackendHooks const& hooks,
                                                           std::atomic<bool>* cancel_requested,
                                                           std::function<bool()> wait_while_paused);

} // namespace z7::app
