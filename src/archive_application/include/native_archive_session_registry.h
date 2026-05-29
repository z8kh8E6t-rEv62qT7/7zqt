// src/archive_application/include/native_archive_session_registry.h
// Role: Public session registry API for native archive preview sessions.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "archive_types.h"
#include "archive_types_base.h"

namespace z7::app {

struct ArchiveOpenSessionState;
struct ArchiveOpenSessionNativeAccess;
struct ArchiveSessionRegistryNativeAccess;
struct ArchiveBackendHooks;

// Error returned by the three-strategy open pipeline. Mirrors the semantics
// that PanelItemOpen.cpp relies on when deciding whether to keep trying or
// bail out.
enum class NestedOpenOutcome {
  kOpened,      // S_OK on some strategy.
  kAborted,     // E_ABORT (user cancel) - do not fall back further.
  kNotArchive,  // S_FALSE - e.g. bytes aren't a recognized archive format.
  kFailed,      // Any other hard failure.
};

class ArchiveSessionRegistry;

// Opaque session representation. The public API exposes only registry-facing
// session metadata; native 7-Zip state stays behind native_7z private
// headers.
class ArchiveOpenSession {
 public:
  ArchiveOpenSession();
  ~ArchiveOpenSession();

  ArchiveOpenSession(const ArchiveOpenSession&) = delete;
  ArchiveOpenSession& operator=(const ArchiveOpenSession&) = delete;

  ArchiveSessionToken token() const {
    return token_;
  }
  const std::string& display_path() const {
    return display_path_;
  }
  OpenArchiveSessionResult::Strategy strategy() const {
    return strategy_;
  }
  const std::string& password() const {
    return password_;
  }
  bool dirty() const {
    return dirty_;
  }
  size_t depth() const;
  void set_password(std::string value);

 private:
  friend class ArchiveSessionRegistry;
  friend struct ArchiveOpenSessionNativeAccess;
  friend OperationResult close_native_archive_session(
      ArchiveSessionRegistry& registry,
      ArchiveSessionToken token,
      const ArchiveBackendHooks& hooks,
      std::atomic<bool>* cancel_requested,
      std::function<bool()> wait_while_paused);

  ArchiveSessionToken token_{};
  std::string display_path_;
  std::string password_;
  std::string source_archive_path_;
  std::string entry_path_from_parent_;
  bool dirty_ = false;
  OpenArchiveSessionResult::Strategy strategy_ =
      OpenArchiveSessionResult::Strategy::kTempFile;

  // Parent session is pinned so the parent's archive outlives this session.
  std::shared_ptr<ArchiveOpenSession> parent_;
  std::unique_ptr<ArchiveOpenSessionState> state_;
};

class ArchiveSessionRegistry {
 public:
  static ArchiveSessionRegistry& instance();

  // Releases the registry's reference on the session. In-flight operations
  // holding a shared_ptr will finish before the session is destroyed.
  bool close(ArchiveSessionToken token);

  // Looks up a session. Returns nullptr if unknown.
  std::shared_ptr<ArchiveOpenSession> find(ArchiveSessionToken token) const;

  // Invoke `fn` with a pinned session reference. Returns false if the token is
  // unknown.
  template <typename Fn>
  bool with_session(ArchiveSessionToken token, Fn&& fn) const {
    auto session = find(token);
    if (!session) {
      return false;
    }
    fn(*session);
    return true;
  }

  // For tests / cleanup.
  size_t session_count() const;

 private:
  friend struct ArchiveSessionRegistryNativeAccess;
  friend OperationResult close_native_archive_session(
      ArchiveSessionRegistry& registry,
      ArchiveSessionToken token,
      const ArchiveBackendHooks& hooks,
      std::atomic<bool>* cancel_requested,
      std::function<bool()> wait_while_paused);

  ArchiveSessionRegistry() = default;

  ArchiveSessionToken allocate_token();
  std::shared_ptr<ArchiveOpenSession> register_session(
      std::shared_ptr<ArchiveOpenSession> session);

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<ArchiveOpenSession>> sessions_;
  std::atomic<uint64_t> next_token_{1};
};

}  // namespace z7::app
