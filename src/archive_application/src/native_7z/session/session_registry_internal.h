// src/archive_application/src/native_7z/session/session_registry_internal.h
// Role: Private native storage and 7-Zip accessors for archive sessions.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "native_archive_session_registry.h"
#include "core/filesystem_replace.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    struct ArchiveBackendHooks;

    struct FilesystemObjectVersion {
        FilesystemObjectIdentity identity;
        uint64_t size = 0;
        int64_t mtime_ticks = 0;
        int64_t change_ticks = 0;
        bool defined = false;
    };

    struct ArchiveOpenSessionState {
        // Bytes backing the child archive. Exactly one of these is populated,
        // matching the strategy value stored on ArchiveOpenSession.
        std::vector<uint8_t> memory_buffer;               // kMemory
        std::unique_ptr<std::filesystem::path> temp_file; // kTempFile (owned path)
        std::filesystem::path temp_dir;                   // kTempFile (unique dir)
        // For kStream, the underlying stream is rooted in the parent archive, so no
        // extra storage is required; the IUnknown ref is held below.
        std::shared_ptr<void> stream_ref_holder; // keeps COM refs alive
        std::optional<FilesystemObjectVersion> source_version;

        // Heavy 7-Zip state.
        std::unique_ptr<CCodecs> codecs;
        std::unique_ptr<CObjectVector<COpenType>> types;
        std::unique_ptr<CIntVector> excluded_formats;
        std::unique_ptr<CArchiveLink> archive_link;
        OpenArchiveDiagnostics open_diagnostics;
    };

    struct ArchiveOpenSessionNativeAccess {
        static ArchiveOpenSessionState& state(ArchiveOpenSession& session) { return *session.state_; }

        static ArchiveOpenSessionState const& state(ArchiveOpenSession const& session) { return *session.state_; }

        static void set_token(ArchiveOpenSession& session, ArchiveSessionToken token) { session.token_ = token; }

        static void set_display_path(ArchiveOpenSession& session, std::string value) {
            session.display_path_ = std::move(value);
        }

        static void set_strategy(ArchiveOpenSession& session, OpenArchiveSessionResult::Strategy strategy) {
            session.strategy_ = strategy;
        }

        static void set_parent(ArchiveOpenSession& session, std::shared_ptr<ArchiveOpenSession> parent) {
            session.parent_ = std::move(parent);
        }

        static std::shared_ptr<ArchiveOpenSession> const& parent(ArchiveOpenSession const& session) {
            return session.parent_;
        }

        static void replace_state(ArchiveOpenSession& session, std::unique_ptr<ArchiveOpenSessionState> state) {
            session.state_ = std::move(state);
        }

        static void set_source_archive_path(ArchiveOpenSession& session, std::string value) {
            session.source_archive_path_ = std::move(value);
        }

        static std::string const& source_archive_path(ArchiveOpenSession const& session) {
            return session.source_archive_path_;
        }

        static void set_entry_path_from_parent(ArchiveOpenSession& session, std::string value) {
            session.entry_path_from_parent_ = std::move(value);
        }

        static std::string const& entry_path_from_parent(ArchiveOpenSession const& session) {
            return session.entry_path_from_parent_;
        }

        static void set_parent_entry_index(ArchiveOpenSession& session, std::optional<uint32_t> value) {
            session.parent_entry_index_ = value;
        }

        static std::optional<uint32_t> parent_entry_index(ArchiveOpenSession const& session) {
            return session.parent_entry_index_;
        }

        static void set_archive_type_hint(ArchiveOpenSession& session, std::string value) {
            session.archive_type_hint_ = std::move(value);
        }

        static std::string const& archive_type_hint(ArchiveOpenSession const& session) {
            return session.archive_type_hint_;
        }

        static void set_dirty(ArchiveOpenSession& session, bool dirty) { session.dirty_ = dirty; }

        static bool dirty(ArchiveOpenSession const& session) { return session.dirty_; }

        static void set_filename_code_page(ArchiveOpenSession& session, FilenameCodePage value) {
            session.filename_code_page_ = value;
        }

        static uint64_t generation(ArchiveOpenSession const& session) { return session.generation_; }

        static void increment_generation(ArchiveOpenSession& session) { ++session.generation_; }

        static void set_generation(ArchiveOpenSession& session, uint64_t generation) {
            session.generation_ = generation;
        }

        static void set_parent_generation_at_open(ArchiveOpenSession& session, uint64_t generation) {
            session.parent_generation_at_open_ = generation;
        }

        static uint64_t parent_generation_at_open(ArchiveOpenSession const& session) {
            return session.parent_generation_at_open_;
        }

        static std::recursive_mutex& operation_mutex(ArchiveOpenSession& session) {
            return session.operation_mutex_;
        }

        static bool closed(ArchiveOpenSession const& session) { return session.closed_; }

        static void set_closed(ArchiveOpenSession& session, bool closed) { session.closed_ = closed; }
    };

    struct ArchiveSessionRegistryNativeAccess {
        static ArchiveSessionToken allocate_token(ArchiveSessionRegistry& registry) {
            return registry.allocate_token();
        }

        static std::shared_ptr<ArchiveOpenSession> register_session(ArchiveSessionRegistry& registry,
                                                                    std::shared_ptr<ArchiveOpenSession> session) {
            return registry.register_session(std::move(session));
        }
    };

    inline ArchiveOpenSessionState& archive_session_state(ArchiveOpenSession& session) {
        return ArchiveOpenSessionNativeAccess::state(session);
    }

    inline ArchiveOpenSessionState const& archive_session_state(ArchiveOpenSession const& session) {
        return ArchiveOpenSessionNativeAccess::state(session);
    }

    inline CArchiveLink& archive_session_link(ArchiveOpenSession& session) {
        return *archive_session_state(session).archive_link;
    }

    inline CCodecs& archive_session_codecs(ArchiveOpenSession& session) {
        return *archive_session_state(session).codecs;
    }

    FilesystemObjectVersion capture_filesystem_object_version(std::filesystem::path const& path,
                                                               std::error_code& ec);
    bool filesystem_object_version_matches(FilesystemObjectVersion const& lhs,
                                           FilesystemObjectVersion const& rhs);

    inline void reset_archive_session_open_state(ArchiveOpenSession& session) {
        ArchiveOpenSessionState& state = archive_session_state(session);
        state.stream_ref_holder.reset();
        state.archive_link = std::make_unique<CArchiveLink>();
        state.types = std::make_unique<CObjectVector<COpenType>>();
        state.excluded_formats = std::make_unique<CIntVector>();
        state.codecs = std::make_unique<CCodecs>();
        state.open_diagnostics = {};
    }

    ArchiveBackendHooks make_session_password_hooks(ArchiveOpenSession& session, ArchiveBackendHooks const& base_hooks);

    std::string archive_item_path_for_matching(CArc const& arc, UInt32 index);

    std::optional<uint64_t> archive_declared_entry_size(CArc const& arc, UInt32 index);

    ExtractInvocationStatus extract_archive_session_entry_to_stream(
        ArchiveOpenSession& session,
        UInt32 entry_index,
        ISequentialOutStream* output_stream,
        ArchiveBackendHooks const& hooks,
        std::atomic<bool>* cancel_requested,
        std::function<bool()> wait_while_paused);

    std::optional<OperationResult> validate_archive_session_parent_item(ArchiveOpenSession const& session,
                                                                        CArc const& parent_arc,
                                                                        UInt32* out_index);

    OpenArchiveSessionResult open_native_archive_session_from_path(ArchiveSessionRegistry& registry,
                                                                   OpenArchiveFromPathRequest const& request,
                                                                   ArchiveBackendHooks const& hooks,
                                                                   std::atomic<bool>* cancel_requested,
                                                                   std::function<bool()> wait_while_paused);

    OpenArchiveFromParentResult open_native_archive_session_from_parent(ArchiveSessionRegistry& registry,
                                                                        OpenArchiveFromParentRequest const& request,
                                                                        ArchiveBackendHooks const& hooks,
                                                                        std::atomic<bool>* cancel_requested,
                                                                        std::function<bool()> wait_while_paused);

    OperationResult set_native_archive_session_filename_code_page(
        ArchiveSessionRegistry& registry,
        SetArchiveSessionFilenameCodePageRequest const& request,
        ArchiveBackendHooks const& hooks,
        std::atomic<bool>* cancel_requested,
        std::function<bool()> wait_while_paused);

    std::optional<OperationResult> ensure_archive_session_writable(ArchiveOpenSession& session,
                                                                   ArchiveBackendHooks const& hooks,
                                                                   std::atomic<bool>* cancel_requested,
                                                                   std::function<bool()> wait_while_paused);

    std::optional<OperationResult> refresh_archive_session_from_backing_file(ArchiveOpenSession& session,
                                                                             ArchiveBackendHooks const& hooks,
                                                                             std::atomic<bool>* cancel_requested,
                                                                             std::function<bool()> wait_while_paused);

    struct SessionMutationBackup {
        std::filesystem::path path;
        FilesystemObjectIdentity identity;
        std::shared_ptr<FilesystemTransaction> transaction;

        bool empty() const { return path.empty(); }
    };

    std::optional<OperationResult> create_archive_session_mutation_backup(ArchiveOpenSession const& session,
                                                                          SessionMutationBackup* backup);
    std::optional<OperationResult> restore_archive_session_mutation_backup(ArchiveOpenSession& session,
                                                                           SessionMutationBackup const& backup,
                                                                           ArchiveBackendHooks const& hooks,
                                                                           std::atomic<bool>* cancel_requested,
                                                                           std::function<bool()> wait_while_paused);
    std::optional<OperationResult> discard_archive_session_mutation_backup(SessionMutationBackup const& backup);

    OperationResult close_native_archive_session(ArchiveSessionRegistry& registry,
                                                 ArchiveSessionToken token,
                                                 NestedDirtyClosePolicy nested_dirty_policy,
                                                 ArchiveBackendHooks const& hooks,
                                                 std::atomic<bool>* cancel_requested,
                                                 std::function<bool()> wait_while_paused);

} // namespace z7::app
