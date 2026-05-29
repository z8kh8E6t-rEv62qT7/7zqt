#include "internal.h"
#include "macos_integration_c_api_menu_helpers.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>
#include <cstring>

#include "filemanager_instance_launcher.h"
#include "json_localization.h"
#include "task_ipc_runtime.h"

using z7::macos_integration::MacOSIntegrationConfigSnapshot;
using z7::shell_integration::ShellIntegrationConfig;
using z7::shell_integration::ShellIntegrationMenuPlan;
using z7::shell_integration::ShellIntegrationSelection;
namespace capi = z7::macos_integration::capi_internal;

namespace {

void consume_task_ipc_completions(const QString& owner_instance_id) {
  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  if (!z7::task_ipc_runtime::collect_task_ipc_events(owner_instance_id, &events,
                                                     nullptr)) {
    return;
  }

  for (const z7::task_ipc_runtime::TaskIpcEvent& task_event : events) {
    z7::task_ipc_runtime::acknowledge_task_ipc_event(task_event, nullptr);
  }
}

void register_task_ipc_completion_consumer(const QString& owner_instance_id) {
  z7::task_ipc_runtime::set_task_ipc_event_notifier(
      owner_instance_id,
      [](const QString& notified_owner_instance_id) {
        consume_task_ipc_completions(notified_owner_instance_id);
      },
      nullptr);
}

z7_mi_status_t dispatch_finder_sync_task_ipc(
    const QString& worker_program,
    const QString& missing_program_name,
    const QString& working_dir,
    const QString& action_id,
    const z7::task_ipc_runtime::TaskIpcPayload& payload,
    z7_mi_action_result_t* out_result) {
  const QFileInfo worker_info(worker_program);
  if (!worker_info.exists()) {
    capi::init_action_result_error(
        out_result,
        Z7_MI_STATUS_IO_ERROR,
        QStringLiteral("Cannot locate program: %1").arg(missing_program_name),
        action_id);
    return Z7_MI_STATUS_IO_ERROR;
  }

  QString bootstrap_error;
  if (!z7::task_ipc_runtime::ensure_task_ipc_bootstrap_ready(&bootstrap_error)) {
    capi::init_action_result_error(out_result,
                                   Z7_MI_STATUS_BACKEND_ERROR,
                                   bootstrap_error.trimmed().isEmpty()
                                       ? QStringLiteral("Failed to initialize task IPC bootstrap.")
                                       : bootstrap_error,
                                   action_id);
    return Z7_MI_STATUS_BACKEND_ERROR;
  }

  const QString owner_instance_id =
      z7::task_ipc_runtime::ensure_task_ipc_owner_instance_id();
  register_task_ipc_completion_consumer(owner_instance_id);
  z7::task_ipc_runtime::TaskIpcDispatchResult dispatch_result;
  QString dispatch_error;
  const bool dispatched = z7::task_ipc_runtime::dispatch_task_ipc_task(
      worker_info.absoluteFilePath(),
      working_dir,
      owner_instance_id,
      payload,
      &dispatch_result,
      &dispatch_error);
  if (!dispatched) {
    capi::init_action_result_error(out_result,
                                   Z7_MI_STATUS_BACKEND_ERROR,
                                   dispatch_error.trimmed().isEmpty()
                                       ? QStringLiteral("Failed to dispatch task IPC task.")
                                       : dispatch_error,
                                   action_id);
    return Z7_MI_STATUS_BACKEND_ERROR;
  }

  out_result->ok = true;
  out_result->status = Z7_MI_STATUS_OK;
  out_result->error_message = nullptr;
  out_result->action_id = capi::duplicate_c_string(action_id);
  return Z7_MI_STATUS_OK;
}

}  // namespace

extern "C" {

z7_mi_status_t z7_mi_execute_menu_action(z7_mi_session_t* session,
                                         const char* action_id,
                                         const z7_mi_selection_t* selection,
                                         z7_mi_action_result_t* out_result) {
  if (session == nullptr || selection == nullptr || out_result == nullptr) {
    capi::init_action_result_error(
        out_result,
        Z7_MI_STATUS_INVALID_ARGUMENT,
        QStringLiteral(
            "Null argument passed to z7_mi_execute_menu_action."),
        capi::to_qstring(action_id));
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  capi::ensure_qt_core_app();
  std::memset(out_result, 0, sizeof(*out_result));

  QString settings_error;
  if (!capi::ensure_portable_settings(session, &settings_error)) {
    capi::init_action_result_error(out_result,
                                   Z7_MI_STATUS_IO_ERROR,
                                   settings_error,
                                   capi::to_qstring(action_id));
    return Z7_MI_STATUS_IO_ERROR;
  }

  ShellIntegrationSelection native_selection;
  native_selection.selected_paths = capi::string_list_from_utf8(
      selection->selected_paths, selection->selected_path_count);
  native_selection.shift_pressed = selection->shift_pressed;
  native_selection.working_directory = capi::to_qstring(selection->working_directory);
  const QString locale_hint = capi::to_qstring(selection->locale_hint);
  const QString q_action_id = capi::to_qstring(action_id);

  QString snapshot_error;
  const MacOSIntegrationConfigSnapshot snapshot =
      z7::macos_integration::load_macos_integration_config_snapshot(
          &snapshot_error);
  ShellIntegrationConfig config = capi::runtime_config_from_snapshot(snapshot);

  const ShellIntegrationMenuPlan plan =
      z7::shell_integration::build_shell_integration_menu_plan(
          native_selection, config, locale_hint);
  if (!plan.menu_visible) {
    capi::init_action_result_error(out_result,
                                   Z7_MI_STATUS_INVALID_ARGUMENT,
                                   QStringLiteral("No valid selection for Finder action."),
                                   q_action_id);
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }

  const auto action_it = std::find_if(
      plan.actions.cbegin(),
      plan.actions.cend(),
      [&q_action_id](const z7::shell_integration::ShellIntegrationMenuAction& action) {
        return action.action_id == q_action_id;
      });
  if (action_it == plan.actions.cend()) {
    capi::init_action_result_error(
        out_result,
        Z7_MI_STATUS_INVALID_ARGUMENT,
        QStringLiteral("Action is not available in menu plan: %1").arg(q_action_id),
        q_action_id);
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }

  const QString first_path =
      plan.selected_paths.isEmpty() ? QString() : plan.selected_paths.front();
  const QString open_as_type = capi::open_as_type_for_action(q_action_id);
  const bool is_open_action = q_action_id == QString::fromLatin1(z7::shell_integration::kActionOpen);
  const bool is_open_as_action = !open_as_type.isEmpty();

  if (is_open_action || is_open_as_action) {
    if (plan.selected_paths.size() != 1 || first_path.isEmpty()) {
      capi::init_action_result_error(out_result,
                                     Z7_MI_STATUS_INVALID_ARGUMENT,
                                     QStringLiteral("Open requires exactly one target path."),
                                     q_action_id);
      return Z7_MI_STATUS_INVALID_ARGUMENT;
    }

    const QString program = capi::bundled_program_path_from_process_dir(
        QCoreApplication::applicationDirPath(),
        QString::fromLatin1(z7::shell_integration::kProgram7zFM));
    QString launch_error;
    if (!z7::platform::qt::filemanager_instance_launcher::
            launch_open_request_for_program(
            program,
            first_path,
            is_open_as_action ? open_as_type : QString(),
            capi::resolve_working_dir(plan, native_selection),
            &launch_error)) {
      capi::init_action_result_error(
          out_result,
          Z7_MI_STATUS_BACKEND_ERROR,
          launch_error.trimmed().isEmpty()
              ? z7::i18n::text(
                    QStringLiteral("ui.navigation.task_ipc.launch_7zfm_failed"))
              : launch_error.trimmed(),
          q_action_id);
      return Z7_MI_STATUS_BACKEND_ERROR;
    }

    out_result->ok = true;
    out_result->status = Z7_MI_STATUS_OK;
    out_result->error_message = nullptr;
    out_result->action_id = capi::duplicate_c_string(q_action_id);
    return Z7_MI_STATUS_OK;
  }

  z7::task_ipc_runtime::TaskIpcPayload payload;
  QString payload_error;
  if (!capi::build_task_ipc_payload_for_action(q_action_id, plan, &payload, &payload_error)) {
    capi::init_action_result_error(out_result,
                                   Z7_MI_STATUS_INVALID_ARGUMENT,
                                   payload_error,
                                   q_action_id);
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }

  const QString worker_program = capi::bundled_program_path_from_process_dir(
      QCoreApplication::applicationDirPath(), QStringLiteral("7zG"));
  return dispatch_finder_sync_task_ipc(
      worker_program,
      QStringLiteral("7zG"),
      capi::resolve_working_dir(plan, native_selection),
      q_action_id,
      payload,
      out_result);
}

void z7_mi_free_action_result(z7_mi_action_result_t* result) {
  if (result == nullptr) {
    return;
  }
  capi::free_c_string(result->error_message);
  capi::free_c_string(result->action_id);
}

}  // extern "C"
