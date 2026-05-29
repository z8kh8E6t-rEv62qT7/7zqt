#include "internal.h"

#include <cstring>

using z7::macos_integration::MacOSIntegrationConfigSnapshot;
using z7::shell_integration::ShellIntegrationConfig;
using z7::shell_integration::ShellIntegrationMenuPlan;
using z7::shell_integration::ShellIntegrationSelection;
namespace capi = z7::macos_integration::capi_internal;

extern "C" {

z7_mi_status_t z7_mi_build_menu_plan(z7_mi_session_t* session,
                                     const z7_mi_selection_t* selection,
                                     z7_mi_menu_plan_t* out_plan) {
  if (session == nullptr || selection == nullptr || out_plan == nullptr) {
    capi::init_menu_plan_error(
        out_plan,
        Z7_MI_STATUS_INVALID_ARGUMENT,
        QStringLiteral("Null argument passed to z7_mi_build_menu_plan."));
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  capi::ensure_qt_core_app();
  std::memset(out_plan, 0, sizeof(*out_plan));

  QString settings_error;
  if (!capi::ensure_portable_settings(session, &settings_error)) {
    capi::init_menu_plan_error(out_plan, Z7_MI_STATUS_IO_ERROR, settings_error);
    return Z7_MI_STATUS_IO_ERROR;
  }

  QString snapshot_error;
  const MacOSIntegrationConfigSnapshot snapshot =
      z7::macos_integration::load_macos_integration_config_snapshot(
          &snapshot_error);
  ShellIntegrationConfig config = capi::runtime_config_from_snapshot(snapshot);

  ShellIntegrationSelection native_selection;
  native_selection.selected_paths = capi::string_list_from_utf8(
      selection->selected_paths, selection->selected_path_count);
  native_selection.shift_pressed = selection->shift_pressed;
  native_selection.working_directory = capi::to_qstring(selection->working_directory);
  const QString locale_hint = capi::to_qstring(selection->locale_hint);

  const ShellIntegrationMenuPlan plan =
      z7::shell_integration::build_shell_integration_menu_plan(
          native_selection, config, locale_hint);

  out_plan->ok = snapshot_error.trimmed().isEmpty();
  out_plan->status =
      out_plan->ok ? Z7_MI_STATUS_OK : Z7_MI_STATUS_INTERNAL_ERROR;
  out_plan->error_message = capi::duplicate_c_string(snapshot_error.trimmed());
  out_plan->menu_visible = plan.menu_visible;
  out_plan->base_folder = capi::duplicate_c_string(plan.base_folder);
  out_plan->extract_subdir = capi::duplicate_c_string(plan.extract_subdir);
  out_plan->archive_name = capi::duplicate_c_string(plan.archive_name);

  const size_t action_count = static_cast<size_t>(plan.actions.size());
  out_plan->action_count = action_count;
  if (action_count == 0) {
    return out_plan->status;
  }

  out_plan->actions = static_cast<z7_mi_menu_action_t*>(
      std::calloc(action_count, sizeof(z7_mi_menu_action_t)));
  if (out_plan->actions == nullptr) {
    z7_mi_free_menu_plan(out_plan);
    capi::init_menu_plan_error(
        out_plan,
        Z7_MI_STATUS_INTERNAL_ERROR,
        QStringLiteral("Out of memory allocating menu actions."));
    return Z7_MI_STATUS_INTERNAL_ERROR;
  }

  for (size_t i = 0; i < action_count; ++i) {
    out_plan->actions[i].action_id = capi::duplicate_c_string(
        plan.actions[static_cast<int>(i)].action_id);
    out_plan->actions[i].title =
        capi::duplicate_c_string(plan.actions[static_cast<int>(i)].title);
  }
  return out_plan->status;
}

void z7_mi_free_menu_plan(z7_mi_menu_plan_t* plan) {
  if (plan == nullptr) {
    return;
  }
  capi::free_c_string(plan->error_message);
  capi::free_c_string(plan->base_folder);
  capi::free_c_string(plan->extract_subdir);
  capi::free_c_string(plan->archive_name);
  if (plan->actions != nullptr) {
    for (size_t i = 0; i < plan->action_count; ++i) {
      capi::free_c_string(plan->actions[i].action_id);
      capi::free_c_string(plan->actions[i].title);
    }
    std::free(plan->actions);
    plan->actions = nullptr;
  }
  plan->action_count = 0;
}

}  // extern "C"
