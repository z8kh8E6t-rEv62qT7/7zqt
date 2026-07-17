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
        QStringList result_messages;
        // True after a structured recoverable archive-open diagnostic has
        // been added to the progress result table.
        bool archive_open_diagnostic_presented = false;
        // True when the runner has already represented the final failure in a
        // modal dialog or in the progress dialog's result messages.
        bool final_error_presented = false;
    };

    class GuiTaskRunner {
    public:
        using FinishedCallback = std::function<void(GuiTaskRunResult)>;

        void run_modal_async(GuiTaskSpec const& spec,
                             QString const& title,
                             QWidget* parent,
                             SharedTaskCancellation cancel_requested,
                             FinishedCallback on_finished);
        GuiTaskRunResult run_modal_blocking_with_dialog(GuiTaskSpec const& spec,
                                                        QString const& title,
                                                        z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                                        SharedTaskCancellation cancel_requested = {});
    };

    bool task_is_test(GuiTaskSpec const& spec);
    uint64_t test_archive_count_hint(GuiTaskSpec const& spec);
    bool task_is_hash(GuiTaskSpec const& spec);

} // namespace z7::ui::gui
