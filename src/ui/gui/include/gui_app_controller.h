#pragma once

#include <functional>
#include <variant>

#include <QString>

#include "gui_task_spec.h"
#include "task_cancellation.h"

namespace z7::task_ipc_runtime {
struct TaskIpcPayload;
}  // namespace z7::task_ipc_runtime

namespace z7::ui::gui {

#ifdef Z7_TESTING
struct BenchmarkCommandOptions;
#endif
struct GuiTaskCompletion {
  int exit_code = 255;
  QString summary;
};

class GuiAppController {
 public:
#ifdef Z7_TESTING
  using BenchmarkDialogInvoker =
      std::function<int(const BenchmarkCommandOptions&)>;
#endif
  using FinishedCallback = std::function<void(const GuiTaskCompletion&)>;

  void run_task_ipc_payload_async(
      const z7::task_ipc_runtime::TaskIpcPayload& payload,
      SharedTaskCancellation cancel_requested,
      FinishedCallback on_finished);
  void run_task_spec_async(const GuiTaskSpec& spec,
                           const QString& title_override,
                           SharedTaskCancellation cancel_requested,
                           FinishedCallback on_finished);

 private:
  void run_task_spec_async_impl(const GuiTaskSpec& requested_spec,
                                const QString& title_override,
                                SharedTaskCancellation cancel_requested,
                                FinishedCallback on_finished);

#ifdef Z7_TESTING
  BenchmarkDialogInvoker benchmark_dialog_invoker_;
#endif
};

}  // namespace z7::ui::gui
