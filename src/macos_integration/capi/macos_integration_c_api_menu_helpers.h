#pragma once

#include "internal.h"
#include "task_ipc_runtime.h"

namespace z7::macos_integration::capi_internal {

    QString open_as_type_for_action(QString const& action_id);
    QString resolve_working_dir(z7::shell_integration::ShellIntegrationMenuPlan const& plan,
                                z7::shell_integration::ShellIntegrationSelection const& selection);
    bool build_task_ipc_payload_for_action(QString const& action_id,
                                           z7::shell_integration::ShellIntegrationMenuPlan const& plan,
                                           z7::task_ipc_runtime::TaskIpcPayload* out_payload,
                                           QString* error_message);

} // namespace z7::macos_integration::capi_internal
