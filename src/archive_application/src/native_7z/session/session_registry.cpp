// src/archive_application/src/native_7z/session/session_registry.cpp
// Role: Nested-archive preview session storage and lifecycle.

#include "session/session_registry_internal.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

#include "common/archive_type_normalization.h"
#include "core/filesystem_replace.h"
#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

namespace {

constexpr std::string_view kSessionRootBackupSuffix = ".z7-session-backup-";

void remove_path_tree(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

std::filesystem::path make_temp_session_dir() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) {
    base = fs::path(".");
  }
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path candidate =
      base / (std::string("z7-session-write-") +
              std::to_string(static_cast<long long>(ticks)));
  for (int i = 0; i < 32; ++i) {
    std::error_code dir_ec;
    if (fs::create_directories(candidate, dir_ec)) {
      return candidate;
    }
    candidate = base /
                (std::string("z7-session-write-") +
                 std::to_string(static_cast<long long>(ticks + i + 1)));
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

std::string archive_session_format_token(const ArchiveOpenSession& session) {
  const ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.archive_link == nullptr || state.codecs == nullptr) {
    return {};
  }
  const CArc* arc = state.archive_link->GetArc();
  if (arc == nullptr || arc->FormatIndex < 0 ||
      static_cast<unsigned>(arc->FormatIndex) >= state.codecs->Formats.Size()) {
    return {};
  }
  const wchar_t* format_name =
      state.codecs->GetFormatNamePtr(static_cast<unsigned>(arc->FormatIndex));
  if (format_name == nullptr || format_name[0] == 0) {
    return {};
  }
  return normalize_session_format_token(ustring_to_utf8(UString(format_name)));
}

std::filesystem::path session_materialized_file_path(const ArchiveOpenSession& session,
                                                     const std::filesystem::path& directory) {
  namespace fs = std::filesystem;
  const ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.temp_file != nullptr && !state.temp_file->empty()) {
    return directory / state.temp_file->filename();
  }
  const std::string& entry_path =
      ArchiveOpenSessionNativeAccess::entry_path_from_parent(session);
  if (!entry_path.empty()) {
    const fs::path entry(entry_path);
    if (!entry.filename().empty()) {
      return directory / entry.filename();
    }
  }
  const std::string& source_path =
      ArchiveOpenSessionNativeAccess::source_archive_path(session);
  if (!source_path.empty()) {
    const fs::path source(source_path);
    if (!source.filename().empty()) {
      return directory / source.filename();
    }
  }
  const fs::path display(session.display_path());
  if (!display.filename().empty()) {
    return directory / display.filename();
  }
  return directory / fs::path("session.bin");
}

std::optional<OperationResult> write_buffer_to_file(const std::vector<uint8_t>& buffer,
                                                    const std::filesystem::path& path) {
  if (path.empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Writable session materialization path is empty",
        2);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Failed to create writable session file: " + path.string(),
        2);
  }
  if (!buffer.empty()) {
    out.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    if (!out.good()) {
      return make_operation_failure<OperationResult>(
          ArchiveErrorDomain::kIo,
          "Failed to write writable session file: " + path.string(),
          2);
    }
  }
  return std::nullopt;
}

std::optional<UInt32> resolve_entry_index_in_parent(ArchiveOpenSession& parent,
                                                    const std::string& entry_path) {
  CArchiveLink& link = archive_session_link(parent);
  const CArc* arc = link.GetArc();
  if (arc == nullptr || arc->Archive == nullptr) {
    return std::nullopt;
  }
  UInt32 num_items = 0;
  if (arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
    return std::nullopt;
  }
  const std::string needle = normalize_archive_item_path(entry_path);
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string candidate = normalize_archive_item_path(
        archive_get_prop_text(arc->Archive, i, kpidPath));
    if (candidate == needle) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<OperationResult> materialize_session_backing_file(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
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

  fs::path dir = make_temp_session_dir();
  if (dir.empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Failed to create writable session temp directory",
        2);
  }
  const fs::path file_path = session_materialized_file_path(session, dir);

  if (state.memory_buffer.empty()) {
    const std::string& source_path =
        ArchiveOpenSessionNativeAccess::source_archive_path(session);
    if (!source_path.empty()) {
      std::error_code copy_ec;
      fs::copy_file(fs::path(source_path),
                    file_path,
                    fs::copy_options::overwrite_existing,
                    copy_ec);
      if (copy_ec) {
        remove_path_tree(dir);
        return make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kIo,
            "Failed to materialize writable root archive: " + copy_ec.message(),
            2);
      }
    } else if (ArchiveOpenSessionNativeAccess::parent(session) != nullptr) {
      const std::string& entry_path =
          ArchiveOpenSessionNativeAccess::entry_path_from_parent(session);
      auto resolved_index = resolve_entry_index_in_parent(
          *ArchiveOpenSessionNativeAccess::parent(session),
          entry_path);
      if (!resolved_index.has_value()) {
        remove_path_tree(dir);
        return make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kInvalidArguments,
            "Nested session entry path not found in parent archive: " + entry_path,
            7);
      }

      const auto& parent = ArchiveOpenSessionNativeAccess::parent(session);
      CArchiveLink& parent_link = archive_session_link(*parent);
      const CArc* parent_arc = parent_link.GetArc();
      if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
        remove_path_tree(dir);
        return make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kIo,
            "Parent archive not available for writable nested session materialization",
            2);
      }

      auto* callback = new NativeExtractCallback(parent_arc->Archive,
                                                 dir,
                                                 hooks,
                                                 cancel_requested,
                                                 std::move(wait_while_paused),
                                                 parent->display_path(),
                                                 {},
                                                 OverwriteMode::kOverwrite,
                                                 ExtractPathMode::kNoPaths,
                                                 std::string{},
                                                 {},
                                                 parent->password(),
                                                 ExtractZoneIdMode::kNone,
                                                 false,
                                                 1);
      const UInt32 indices[1] = {*resolved_index};
      const HRESULT extract_hr =
          parent_arc->Archive->Extract(indices, 1, /*testMode=*/0, callback);
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
        for (const auto& entry : fs::directory_iterator(dir, it_ec)) {
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
          ArchiveErrorDomain::kIo,
          "Session has no writable materialization source",
          2);
    }
  } else {
    if (std::optional<OperationResult> write_error =
            write_buffer_to_file(state.memory_buffer, file_path);
        write_error.has_value()) {
      remove_path_tree(dir);
      return write_error;
    }
  }

  *out_file_path = file_path;
  *out_dir_path = dir;
  return std::nullopt;
}

std::optional<OperationResult> reopen_archive_session_from_path(
    ArchiveOpenSession& session,
    const std::filesystem::path& file_path,
    const std::filesystem::path& dir_path,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  if (file_path.empty()) {
    return invalid_request("Session reopen requires a materialized archive file");
  }

  const std::string format_hint = archive_session_format_token(session);
  auto next_state = std::make_unique<ArchiveOpenSessionState>();
  next_state->temp_dir = dir_path;
  next_state->temp_file = std::make_unique<std::filesystem::path>(file_path);
  next_state->archive_link = std::make_unique<CArchiveLink>();
  next_state->types = std::make_unique<CObjectVector<COpenType>>();
  next_state->excluded_formats = std::make_unique<CIntVector>();
  next_state->codecs = std::make_unique<CCodecs>();

  const ArchiveBackendHooks reopen_hooks =
      make_session_password_hooks(session, hooks);
  const CArc* arc = nullptr;
  bool password_requested = false;
  bool wrong_password = false;
  std::string password;
  const HRESULT hr = open_archive_shared(
      file_path.string(),
      format_hint,
      reopen_hooks,
      cancel_requested,
      std::move(wait_while_paused),
      /*enable_open_callback=*/true,
      /*codecs_already_loaded=*/false,
      *next_state->codecs,
      *next_state->types,
      *next_state->excluded_formats,
      *next_state->archive_link,
      arc,
      &password_requested,
      &wrong_password,
      &password);
  if (hr != S_OK) {
    if (password_requested || wrong_password || !password.empty()) {
      return make_operation_failure<OperationResult>(
          ArchiveErrorDomain::kPassword,
          "Password required or incorrect",
          2);
    }
    if (hr == E_ABORT) {
      return make_operation_canceled<OperationResult>();
    }
    return make_operation_failure_from_hresult<OperationResult>(hr);
  }

  if (!password.empty()) {
    session.set_password(std::move(password));
  }
  ArchiveOpenSessionNativeAccess::replace_state(session, std::move(next_state));
  ArchiveOpenSessionNativeAccess::set_strategy(
      session,
      OpenArchiveSessionResult::Strategy::kTempFile);
  return std::nullopt;
}

std::optional<OperationResult> commit_archive_session_to_parent(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  (void)cancel_requested;
  (void)wait_while_paused;
  const auto& parent = ArchiveOpenSessionNativeAccess::parent(session);
  if (parent == nullptr) {
    return invalid_request("Parent session commit requires a parent session");
  }

  ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.temp_file == nullptr || state.temp_file->empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Dirty nested session has no writable archive file",
        2);
  }

  const std::string parent_format = archive_session_format_token(*parent);
  if (parent_format.empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kUnsupportedFormat,
        "Cannot determine parent archive format for nested session commit",
        2);
  }

  AddRequest request;
  request.session_token = parent->token();
  request.format = parent_format;
  request.update_mode = "update";
  request.password = parent->password();
  request.input_items.push_back(
      AddInputItem{state.temp_file->string(),
                   ArchiveOpenSessionNativeAccess::entry_path_from_parent(session)});

  NativeArchiveBackend backend;
  const AddResult add_result = backend.add(request, hooks);
  if (!add_result.ok) {
    return static_cast<OperationResult>(add_result);
  }

  ArchiveOpenSessionNativeAccess::set_dirty(session, false);
  return std::nullopt;
}

std::optional<OperationResult> commit_archive_session_to_root(
    ArchiveOpenSession& session) {
  ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.temp_file == nullptr || state.temp_file->empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Dirty root session has no writable archive file",
        2);
  }
  const std::string& source_path =
      ArchiveOpenSessionNativeAccess::source_archive_path(session);
  if (source_path.empty()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kInvalidArguments,
        "Root session is missing its source archive path",
        7);
  }

  const AtomicReplaceResult replace_result = replace_file_atomically(
      *state.temp_file,
      std::filesystem::path(source_path),
      kSessionRootBackupSuffix);
  if (!replace_result.success) {
    if (replace_result.error.has_value()) {
      return replace_result.error;
    }
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kIo,
        "Failed to commit writable root archive session",
        2);
  }

  ArchiveOpenSessionNativeAccess::set_dirty(session, false);
  return std::nullopt;
}

}  // namespace

ArchiveBackendHooks make_session_password_hooks(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& base_hooks) {
  ArchiveBackendHooks hooks = base_hooks;
  hooks.ask_password = [&session, base = base_hooks.ask_password](
                           const PasswordPrompt& prompt) {
    if (prompt.reason_kind == PasswordPromptReason::kWrongPassword) {
      session.set_password({});
    }
    if (!session.password().empty()) {
      PasswordReply reply;
      reply.kind = PasswordReplyKind::kProvide;
      reply.password = session.password();
      return reply;
    }
    if (base) {
      const PasswordReply reply = base(prompt);
      if (reply.kind == PasswordReplyKind::kProvide && !reply.password.empty()) {
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

ArchiveOpenSession::ArchiveOpenSession()
    : state_(std::make_unique<ArchiveOpenSessionState>()) {}

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

std::shared_ptr<ArchiveOpenSession> ArchiveSessionRegistry::register_session(
    std::shared_ptr<ArchiveOpenSession> session) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_[session->token_.value] = session;
  return session;
}

bool ArchiveSessionRegistry::close(ArchiveSessionToken token) {
  const OperationResult result = close_native_archive_session(
      *this,
      token,
      {},
      nullptr,
      [] { return false; });
  return result.ok;
}

std::shared_ptr<ArchiveOpenSession> ArchiveSessionRegistry::find(
    ArchiveSessionToken token) const {
  if (!token.is_valid()) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(token.value);
  if (it == sessions_.end()) {
    return nullptr;
  }
  return it->second;
}

size_t ArchiveSessionRegistry::session_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

std::optional<OperationResult> ensure_archive_session_writable(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.temp_file != nullptr && !state.temp_file->empty()) {
    return std::nullopt;
  }

  std::filesystem::path file_path;
  std::filesystem::path dir_path;
  if (std::optional<OperationResult> materialize_error =
          materialize_session_backing_file(
              session,
              hooks,
              cancel_requested,
              wait_while_paused,
              &file_path,
              &dir_path);
      materialize_error.has_value()) {
    return materialize_error;
  }

  return reopen_archive_session_from_path(
      session,
      file_path,
      dir_path,
      hooks,
      cancel_requested,
      std::move(wait_while_paused));
}

std::optional<OperationResult> refresh_archive_session_from_backing_file(
    ArchiveOpenSession& session,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  const ArchiveOpenSessionState& state = archive_session_state(session);
  if (state.temp_file == nullptr || state.temp_file->empty()) {
    return invalid_request("Writable session refresh requires a backing file");
  }
  return reopen_archive_session_from_path(
      session,
      *state.temp_file,
      state.temp_dir,
      hooks,
      cancel_requested,
      std::move(wait_while_paused));
}

OperationResult close_native_archive_session(
    ArchiveSessionRegistry& registry,
    ArchiveSessionToken token,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  (void)cancel_requested;
  (void)wait_while_paused;
  if (!token.is_valid()) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kInvalidArguments,
        "Unknown archive session token",
        7);
  }

  std::shared_ptr<ArchiveOpenSession> session = registry.find(token);
  if (!session) {
    return make_operation_failure<OperationResult>(
        ArchiveErrorDomain::kInvalidArguments,
        "Unknown archive session token",
        7);
  }

  if (ArchiveOpenSessionNativeAccess::dirty(*session)) {
    if (ArchiveOpenSessionNativeAccess::parent(*session) != nullptr) {
      if (std::optional<OperationResult> commit_error =
              commit_archive_session_to_parent(
                  *session,
                  hooks,
                  cancel_requested,
                  wait_while_paused);
          commit_error.has_value()) {
        return std::move(*commit_error);
      }
    } else {
      if (std::optional<OperationResult> commit_error =
              commit_archive_session_to_root(*session);
          commit_error.has_value()) {
        return std::move(*commit_error);
      }
    }
  }

  std::shared_ptr<ArchiveOpenSession> dropped;
  {
    std::lock_guard<std::mutex> lock(registry.mutex_);
    auto it = registry.sessions_.find(token.value);
    if (it == registry.sessions_.end()) {
      return make_operation_failure<OperationResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    dropped = std::move(it->second);
    registry.sessions_.erase(it);
  }
  dropped.reset();
  return make_operation_success<OperationResult>("Session closed");
}

}  // namespace z7::app
