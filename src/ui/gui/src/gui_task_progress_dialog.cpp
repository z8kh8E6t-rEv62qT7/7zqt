#include "gui_task_progress_dialog.h"

namespace z7::ui::gui {

namespace {

const z7::ui::runtime_support::TaskProgressDialogBehavior& gui_task_progress_behavior() {
  static const z7::ui::runtime_support::TaskProgressDialogBehavior behavior = {
      .initial_width = 840,
      .initial_height = 500,
      .modal = true,
      .delete_on_close = false,
      .running_close_requests_cancel = true,
      .confirm_cancel_only_for_test_mode = false,
      .running_stage_uses_test_caption = true,
      .normalize_metric_label_colons = false,
      .append_blank_log_lines = false,
      .parse_extended_progress_log = false,
      .freeze_title_after_result_mode = false,
#ifdef Z7_TESTING
      .dialog_object_name = "taskProgressDialog",
      .result_messages_view_object_name = "taskProgressMessagesList",
      .background_button_object_name = "taskProgressBackgroundButton",
      .pause_button_object_name = "taskProgressPauseButton",
      .cancel_button_object_name = "taskProgressCancelButton",
      .close_button_object_name = "taskProgressCloseButton",
#endif
  };
  return behavior;
}

}  // namespace

TaskProgressDialog::TaskProgressDialog(QWidget* parent)
    : z7::ui::runtime_support::TaskProgressDialogBase(gui_task_progress_behavior(), parent) {}

}  // namespace z7::ui::gui
