#include "filemanager_task_progress_dialog.h"

namespace z7::ui::filemanager {

namespace {

const z7::ui::runtime_support::TaskProgressDialogBehavior& filemanager_task_progress_behavior() {
  static const z7::ui::runtime_support::TaskProgressDialogBehavior behavior = {
      .modal = false,
      .delete_on_close = true,
      .running_close_requests_cancel = false,
      .confirm_cancel_only_for_test_mode = true,
      .running_stage_uses_test_caption = false,
      .normalize_metric_label_colons = true,
      .append_blank_log_lines = true,
      .parse_extended_progress_log = true,
      .freeze_title_after_result_mode = true,
  };
  return behavior;
}

}  // namespace

TaskProgressDialog::TaskProgressDialog(QWidget* parent)
    : z7::ui::runtime_support::TaskProgressDialogBase(
          filemanager_task_progress_behavior(), parent) {}

void TaskProgressDialog::set_result_mode() {
  set_result_mode_impl();
}

}  // namespace z7::ui::filemanager
