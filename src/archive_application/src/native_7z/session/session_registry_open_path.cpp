// src/archive_application/src/native_7z/session/session_registry_open_path.cpp
// Role: Filesystem-path native archive session open flow.

#include "session/session_registry_internal.h"

#include <utility>

#include "core/internal.h"

namespace z7::app {

OpenArchiveSessionResult open_native_archive_session_from_path(
    ArchiveSessionRegistry& registry,
    const OpenArchiveFromPathRequest& request,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  OpenArchiveSessionResult result;
  if (request.archive_path.empty()) {
    static_cast<OperationResult&>(result) =
        make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kInvalidArguments,
            "OpenArchiveFromPath requires archive path",
            7);
    return result;
  }

  auto session = std::make_shared<ArchiveOpenSession>();
  ArchiveOpenSessionNativeAccess::set_display_path(*session, request.archive_path);
  ArchiveOpenSessionNativeAccess::set_source_archive_path(*session, request.archive_path);
  ArchiveOpenSessionNativeAccess::set_strategy(
      *session,
      OpenArchiveSessionResult::Strategy::kTempFile);  // placeholder
  reset_archive_session_open_state(*session);
  ArchiveOpenSessionState& session_state = archive_session_state(*session);

  const CArc* arc = nullptr;
  bool password_requested = false;
  bool wrong_password = false;
  std::string password;
  const HRESULT hr = open_archive_shared(request.archive_path,
                                         request.archive_type_hint,
                                         hooks,
                                         cancel_requested,
                                         std::move(wait_while_paused),
                                         /*enable_open_callback=*/true,
                                         /*codecs_already_loaded=*/false,
                                         *session_state.codecs,
                                         *session_state.types,
                                         *session_state.excluded_formats,
                                         *session_state.archive_link,
                                         arc,
                                         &password_requested,
                                         &wrong_password,
                                         &password);
  if (hr != S_OK) {
    if (password_requested || wrong_password || !password.empty()) {
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kPassword,
              "Password required or incorrect",
              2);
    } else {
      static_cast<OperationResult&>(result) =
          make_operation_failure_from_hresult<OperationResult>(hr);
    }
    return result;
  }
  session->set_password(std::move(password));

  ArchiveOpenSessionNativeAccess::set_token(
      *session,
      ArchiveSessionRegistryNativeAccess::allocate_token(registry));
  ArchiveSessionRegistryNativeAccess::register_session(registry, session);

  static_cast<OperationResult&>(result) =
      make_operation_success<OperationResult>("Archive opened");
  result.token = session->token();
  result.used_strategy =
      OpenArchiveSessionResult::Strategy::kStream;  // not nested; label as stream
  result.archive_path = request.archive_path;
  return result;
}

}  // namespace z7::app
