// src/archive_application/src/native_7z/operations/operations_add.cpp
// Role: Native add/update archive operation.

#include "core/internal.h"
#include "third_party_adapter/callbacks_update.h"
#include "operations/operations_add_input_items.h"
#include "session/session_registry_internal.h"
#include "common/archive_type_normalization.h"

#include <filesystem>

namespace z7::app {
namespace {

constexpr std::string_view kUpdatePairStateIds = "pqrxyzw";
constexpr int kUpdatePairStateNotSupportedActions[] = {2, 2, 1, -1, -1, -1, -1};
constexpr char kUpdateIgnoreSelfCommand[] = "-";
constexpr char kUpdateNewArchivePostChar = '!';

NUpdateArchive::CActionSet action_set_for_mode(const std::string& update_mode) {
  const std::string mode = z7::common::to_lower_ascii_copy(update_mode);
  if (mode == "update") {
    return NUpdateArchive::k_ActionSet_Update;
  }
  if (mode == "fresh") {
    return NUpdateArchive::k_ActionSet_Fresh;
  }
  if (mode == "sync") {
    return NUpdateArchive::k_ActionSet_Sync;
  }
  return NUpdateArchive::k_ActionSet_Add;
}

bool parse_pair_action(char action_char, NUpdateArchive::NPairAction::EEnum* out) {
  if (out == nullptr) {
    return false;
  }
  if (action_char < '0' || action_char > '3') {
    return false;
  }
  *out = static_cast<NUpdateArchive::NPairAction::EEnum>(action_char - '0');
  return true;
}

struct ParsedRawUpdateSwitch final {
  bool ignore_main_archive = false;
  NUpdateArchive::CActionSet action_set;
  std::string post_string;
};

bool parse_raw_update_switch(const std::string& raw_switch,
                             const NUpdateArchive::CActionSet& default_action_set,
                             ParsedRawUpdateSwitch* out) {
  if (out == nullptr) {
    return false;
  }

  const std::string raw = z7::common::trim_ascii_space_copy(raw_switch);
  if (raw.empty()) {
    return false;
  }

  out->ignore_main_archive = false;
  out->action_set = default_action_set;
  out->post_string.clear();

  if (raw == kUpdateIgnoreSelfCommand) {
    out->ignore_main_archive = true;
    return true;
  }

  size_t index = 0;
  while (index < raw.size()) {
    const char state_id = static_cast<char>(
        std::tolower(static_cast<unsigned char>(raw[index])));
    const size_t state_pos = kUpdatePairStateIds.find(state_id);
    if (state_pos == std::string_view::npos) {
      out->post_string = raw.substr(index);
      return true;
    }

    ++index;
    if (index >= raw.size()) {
      return false;
    }

    NUpdateArchive::NPairAction::EEnum action = NUpdateArchive::NPairAction::kIgnore;
    if (!parse_pair_action(raw[index], &action)) {
      return false;
    }
    out->action_set.StateActions[state_pos] = action;
    if (kUpdatePairStateNotSupportedActions[state_pos] == static_cast<int>(action)) {
      return false;
    }

    ++index;
  }

  return true;
}

bool apply_update_action_set(const AddRequest& request,
                             CUpdateOptions& options,
                             std::string* error_summary) {
  options.UpdateArchiveItself = true;
  options.Commands.Clear();

  CUpdateArchiveCommand main_command;
  main_command.ActionSet = action_set_for_mode(request.update_mode);
  options.Commands.Add(main_command);

  std::vector<std::string> raw_switches;
  if (!request.raw_update_switches.empty()) {
    raw_switches = request.raw_update_switches;
  } else if (!request.raw_update_switch.empty()) {
    raw_switches.push_back(request.raw_update_switch);
  }

  for (const std::string& raw_switch : raw_switches) {
    ParsedRawUpdateSwitch parsed;
    if (!parse_raw_update_switch(raw_switch, main_command.ActionSet, &parsed)) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid -u switch action set: " + raw_switch;
      }
      return false;
    }

    if (parsed.ignore_main_archive) {
      if (options.UpdateArchiveItself) {
        options.UpdateArchiveItself = false;
        if (!options.Commands.IsEmpty()) {
          options.Commands.Delete(0);
        }
      }
      continue;
    }

    if (parsed.post_string.empty()) {
      if (options.UpdateArchiveItself && !options.Commands.IsEmpty()) {
        options.Commands[0].ActionSet = parsed.action_set;
      }
      continue;
    }

    if (parsed.post_string.front() != kUpdateNewArchivePostChar) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid -u switch post-string: " + raw_switch;
      }
      return false;
    }
    const std::string user_archive_path = parsed.post_string.substr(1);
    if (user_archive_path.empty()) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid -u switch target archive: " + raw_switch;
      }
      return false;
    }

    CUpdateArchiveCommand redirected_command;
    redirected_command.ActionSet = parsed.action_set;
    redirected_command.UserArchivePath = utf8_to_ustring(user_archive_path);
    options.Commands.Add(redirected_command);
  }

  return true;
}

#ifndef _WIN32
void mark_sfx_archive_executable(const AddRequest& request, AddResult* result) {
  if (result == nullptr || !result->ok || !request.create_sfx ||
      request.archive_path.empty()) {
    return;
  }

  std::error_code ec;
  std::filesystem::permissions(
      std::filesystem::path(request.archive_path),
      std::filesystem::perms::owner_exec |
          std::filesystem::perms::group_exec |
          std::filesystem::perms::others_exec,
      std::filesystem::perm_options::add,
      ec);
  if (!ec) {
    return;
  }

  *result = make_operation_failure<AddResult>(
      ArchiveErrorDomain::kIo,
      "Failed to mark SFX archive executable: " + ec.message(),
      2);
}
#endif

}  // namespace

bool apply_add_runtime_options(const AddRequest& request,
                               CUpdateOptions& options,
                               std::string* error_summary);

std::string session_format_token(const ArchiveOpenSession& session) {
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
  return z7::common::canonical_archive_type_token_copy(
      ustring_to_utf8(UString(format_name)));
}

AddResult NativeArchiveBackend::add(const AddRequest& request,
                                    const ArchiveBackendHooks& hooks) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<AddResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    if (!request.password.empty()) {
      session->set_password(request.password);
    }
    if (std::optional<OperationResult> materialize_error =
            ensure_archive_session_writable(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        materialize_error.has_value()) {
      return from_base_result<AddResult>(std::move(*materialize_error));
    }

    const ArchiveOpenSessionState& state = archive_session_state(*session);
    if (state.temp_file == nullptr || state.temp_file->empty()) {
      return make_operation_failure<AddResult>(
          ArchiveErrorDomain::kIo,
          "Writable archive session does not have a backing file",
          2);
    }

    AddRequest writable_request = request;
    writable_request.session_token.reset();
    writable_request.archive_path = state.temp_file->string();
    writable_request.password = session->password();
    if (writable_request.format.empty()) {
      writable_request.format = session_format_token(*session);
    }

    AddResult add_result = add(writable_request, hooks);
    if (!add_result.ok) {
      return add_result;
    }
    ArchiveOpenSessionNativeAccess::set_dirty(*session, true);
    if (std::optional<OperationResult> refresh_error =
            refresh_archive_session_from_backing_file(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        refresh_error.has_value()) {
      return from_base_result<AddResult>(std::move(*refresh_error));
    }
    return add_result;
  }

  ScopedAddInputTree staged_inputs;
  AddRequest prepared_request;
  AddResult add_result = run_update_operation_with_mode<AddResult>(
      request.archive_path,
      hooks,
      0,
      [&]() {
        return NativeUpdateOperationCallback(
            hooks,
            &cancel_requested_,
            [this]() { return this->wait_while_paused(); },
            request.archive_path,
            NativeUpdateOperationCallback::Mode::kAdd,
            request.password);
      },
      [&](CCodecs& codecs,
          CObjectVector<COpenType>& types,
          NWildcard::CCensor& censor,
          CUpdateOptions& options) -> std::optional<OperationResult> {
        if (std::optional<OperationResult> prepare_error =
                prepare_add_request_for_execution(request, &staged_inputs, &prepared_request);
            prepare_error.has_value()) {
          return std::move(*prepare_error);
        }

        if (!ParseOpenTypes(codecs,
                            utf8_to_ustring(prepared_request.format),
                            types) ||
            types.IsEmpty()) {
          return static_cast<OperationResult>(
              make_operation_failure<AddResult>(ArchiveErrorDomain::kUnsupportedFormat,
                                                "Unsupported archive format",
                                                2));
        }

        for (const std::string& input : prepared_request.input_paths) {
          censor.AddPreItem_NoWildcard(utf8_to_ustring(input));
        }

        std::string update_switch_error;
        if (!apply_update_action_set(prepared_request, options, &update_switch_error)) {
          return static_cast<OperationResult>(
              make_operation_failure<AddResult>(ArchiveErrorDomain::kInvalidArguments,
                                                std::move(update_switch_error),
                                                7));
        }
        if (!apply_add_runtime_options(prepared_request, options, &update_switch_error)) {
          return static_cast<OperationResult>(
              make_operation_failure<AddResult>(ArchiveErrorDomain::kInvalidArguments,
                                                std::move(update_switch_error),
                                                7));
        }
        const std::string normalized_directory =
            normalize_archive_virtual_directory(prepared_request.directory);
        if (!normalized_directory.empty()) {
          options.AddPathPrefix = utf8_to_ustring(normalized_directory + '/');
        }
        options.ArcNameMode = k_ArcNameMode_Exact;
        return std::nullopt;
      });
#ifndef _WIN32
  mark_sfx_archive_executable(prepared_request, &add_result);
#endif
  return add_result;
}

}  // namespace z7::app
