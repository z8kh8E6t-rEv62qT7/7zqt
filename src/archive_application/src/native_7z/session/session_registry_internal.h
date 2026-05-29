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

#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

struct ArchiveBackendHooks;

struct ArchiveOpenSessionState {
  // Bytes backing the child archive. Exactly one of these is populated,
  // matching the strategy value stored on ArchiveOpenSession.
  std::vector<uint8_t> memory_buffer;                // kMemory
  std::unique_ptr<std::filesystem::path> temp_file;  // kTempFile (owned path)
  std::filesystem::path temp_dir;                    // kTempFile (unique dir)
  // For kStream, the underlying stream is rooted in the parent archive, so no
  // extra storage is required; the IUnknown ref is held below.
  std::shared_ptr<void> stream_ref_holder;           // keeps COM refs alive

  // Heavy 7-Zip state.
  std::unique_ptr<CCodecs> codecs;
  std::unique_ptr<CObjectVector<COpenType>> types;
  std::unique_ptr<CIntVector> excluded_formats;
  std::unique_ptr<CArchiveLink> archive_link;
};

struct ArchiveOpenSessionNativeAccess {
  static ArchiveOpenSessionState& state(ArchiveOpenSession& session) {
    return *session.state_;
  }

  static const ArchiveOpenSessionState& state(const ArchiveOpenSession& session) {
    return *session.state_;
  }

  static void set_token(ArchiveOpenSession& session, ArchiveSessionToken token) {
    session.token_ = token;
  }

  static void set_display_path(ArchiveOpenSession& session, std::string value) {
    session.display_path_ = std::move(value);
  }

  static void set_strategy(ArchiveOpenSession& session,
                           OpenArchiveSessionResult::Strategy strategy) {
    session.strategy_ = strategy;
  }

  static void set_parent(ArchiveOpenSession& session,
                         std::shared_ptr<ArchiveOpenSession> parent) {
    session.parent_ = std::move(parent);
  }

  static const std::shared_ptr<ArchiveOpenSession>& parent(
      const ArchiveOpenSession& session) {
    return session.parent_;
  }

  static void replace_state(ArchiveOpenSession& session,
                            std::unique_ptr<ArchiveOpenSessionState> state) {
    session.state_ = std::move(state);
  }

  static void set_source_archive_path(ArchiveOpenSession& session,
                                      std::string value) {
    session.source_archive_path_ = std::move(value);
  }

  static const std::string& source_archive_path(const ArchiveOpenSession& session) {
    return session.source_archive_path_;
  }

  static void set_entry_path_from_parent(ArchiveOpenSession& session,
                                         std::string value) {
    session.entry_path_from_parent_ = std::move(value);
  }

  static const std::string& entry_path_from_parent(
      const ArchiveOpenSession& session) {
    return session.entry_path_from_parent_;
  }

  static void set_dirty(ArchiveOpenSession& session, bool dirty) {
    session.dirty_ = dirty;
  }

  static bool dirty(const ArchiveOpenSession& session) {
    return session.dirty_;
  }
};

struct ArchiveSessionRegistryNativeAccess {
  static ArchiveSessionToken allocate_token(ArchiveSessionRegistry& registry) {
    return registry.allocate_token();
  }

  static std::shared_ptr<ArchiveOpenSession> register_session(
      ArchiveSessionRegistry& registry,
      std::shared_ptr<ArchiveOpenSession> session) {
    return registry.register_session(std::move(session));
  }
};

inline ArchiveOpenSessionState& archive_session_state(ArchiveOpenSession& session) {
  return ArchiveOpenSessionNativeAccess::state(session);
}

inline const ArchiveOpenSessionState& archive_session_state(
    const ArchiveOpenSession& session) {
  return ArchiveOpenSessionNativeAccess::state(session);
}

inline CArchiveLink& archive_session_link(ArchiveOpenSession& session) {
  return *archive_session_state(session).archive_link;
}

inline CCodecs& archive_session_codecs(ArchiveOpenSession& session) {
  return *archive_session_state(session).codecs;
}

inline void reset_archive_session_open_state(ArchiveOpenSession& session) {
  ArchiveOpenSessionState& state = archive_session_state(session);
  state.stream_ref_holder.reset();
  state.archive_link = std::make_unique<CArchiveLink>();
  state.types = std::make_unique<CObjectVector<COpenType>>();
  state.excluded_formats = std::make_unique<CIntVector>();
  state.codecs = std::make_unique<CCodecs>();
}

ArchiveBackendHooks make_session_password_hooks(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& base_hooks);

OpenArchiveSessionResult open_native_archive_session_from_path(
    ArchiveSessionRegistry& registry,
    const OpenArchiveFromPathRequest& request,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused);

OpenArchiveSessionResult open_native_archive_session_from_parent(
    ArchiveSessionRegistry& registry,
    const OpenArchiveFromParentRequest& request,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused);

std::optional<OperationResult> ensure_archive_session_writable(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused);

std::optional<OperationResult> refresh_archive_session_from_backing_file(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused);

OperationResult close_native_archive_session(
    ArchiveSessionRegistry& registry,
    ArchiveSessionToken token,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused);

}  // namespace z7::app
