#pragma once

#include <QStringList>

#include <functional>

#include "archive_types.h"
#include "gui_task_spec.h"
#include "task_cancellation.h"

class QWidget;

namespace z7::ui::runtime_support {
class TaskProgressDialogBase;
}

namespace z7::ui::gui {

struct GuiTaskRunResult {
  z7::app::OperationResult result;
  QStringList log_lines;
  QStringList failure_messages;
  bool failure_displayed = false;
};

class GuiTaskRunner {
 public:
  using FinishedCallback = std::function<void(GuiTaskRunResult)>;

  void run_modal_async(const GuiTaskSpec& spec,
                       const QString& title,
                       QWidget* parent,
                       SharedTaskCancellation cancel_requested,
                       FinishedCallback on_finished);
  GuiTaskRunResult run_modal_blocking_with_dialog(
      const GuiTaskSpec& spec,
      const QString& title,
      z7::ui::runtime_support::TaskProgressDialogBase* dialog,
      SharedTaskCancellation cancel_requested = {});
};

bool task_is_test(const GuiTaskSpec& spec);
uint64_t test_archive_count_hint(const GuiTaskSpec& spec);
bool task_is_hash(const GuiTaskSpec& spec);

}  // namespace z7::ui::gui
