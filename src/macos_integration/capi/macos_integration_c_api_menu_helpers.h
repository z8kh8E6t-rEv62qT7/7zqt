#pragma once

#include "internal.h"

#include "task_ipc_runtime.h"

namespace z7::macos_integration::capi_internal {

QString open_as_type_for_action(const QString& action_id);
QString resolve_working_dir(
    const z7::shell_integration::ShellIntegrationMenuPlan& plan,
    const z7::shell_integration::ShellIntegrationSelection& selection);
bool build_task_ipc_payload_for_action(
    const QString& action_id,
    const z7::shell_integration::ShellIntegrationMenuPlan& plan,
    z7::task_ipc_runtime::TaskIpcPayload* out_payload,
    QString* error_message);

}  // namespace z7::macos_integration::capi_internal
