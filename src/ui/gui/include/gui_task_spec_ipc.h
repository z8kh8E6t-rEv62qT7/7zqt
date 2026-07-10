#pragma once

#include <QString>
#include <optional>

#include "gui_task_spec.h"

namespace z7::task_ipc_runtime {

    struct TaskIpcPayload;

} // namespace z7::task_ipc_runtime

namespace z7::ui::gui {

    std::optional<GuiTaskSpec>
    build_task_spec_from_task_ipc_payload(z7::task_ipc_runtime::TaskIpcPayload const& payload, QString* error_message);

} // namespace z7::ui::gui
