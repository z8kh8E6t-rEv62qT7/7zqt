#pragma once

#include <optional>

#include <QString>

#include "gui_task_spec.h"

namespace z7::task_ipc_runtime {
struct TaskIpcPayload;
}  // namespace z7::task_ipc_runtime

namespace z7::ui::gui {

std::optional<GuiTaskSpec> build_task_spec_from_task_ipc_payload(
    const z7::task_ipc_runtime::TaskIpcPayload& payload,
    QString* error_message);

}  // namespace z7::ui::gui
