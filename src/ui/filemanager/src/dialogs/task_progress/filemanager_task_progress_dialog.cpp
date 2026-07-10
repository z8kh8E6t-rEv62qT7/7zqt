#include "filemanager_task_progress_dialog.h"

namespace z7::ui::filemanager {

    namespace {

        z7::ui::runtime_support::TaskProgressDialogBehavior const& filemanager_task_progress_behavior() {
            static z7::ui::runtime_support::TaskProgressDialogBehavior const behavior = {
                .modal = false,
                .delete_on_close = true,
                .running_close_requests_cancel = false,
                .confirm_cancel_only_for_test_mode = true,
                .running_stage_uses_test_caption = false,
                .normalize_metric_label_colons = true,
                .append_blank_log_lines = true,
                .parse_extended_progress_log = true,
                .freeze_title_after_result_mode = true,
#ifdef Z7_TESTING
                .dialog_object_name = "filemanagerTaskProgressDialog",
                .result_messages_view_object_name = "filemanagerTaskProgressMessages",
                .background_button_object_name = "filemanagerTaskProgressBackgroundButton",
                .pause_button_object_name = "filemanagerTaskProgressPauseButton",
                .cancel_button_object_name = "filemanagerTaskProgressCancelButton",
                .close_button_object_name = "filemanagerTaskProgressCloseButton",
#endif
            };
            return behavior;
        }

    } // namespace

    TaskProgressDialog::TaskProgressDialog(QWidget* parent) :
        z7::ui::runtime_support::TaskProgressDialogBase(filemanager_task_progress_behavior(), parent) {}

    void TaskProgressDialog::set_result_mode() {
        set_result_mode_impl();
    }

} // namespace z7::ui::filemanager
