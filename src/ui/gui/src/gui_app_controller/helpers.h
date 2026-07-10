// src/ui/gui/src/gui_app_controller/helpers.h
// Role: Internal helper routines for GuiAppController command processing.

#pragma once

#include <QString>
#include <cstdint>
#include <string>

class QWidget;

#include "archive_types.h"
#include "gui_task_runner.h"
#include "gui_task_spec.h"

namespace z7::ui::gui::gui_app_controller_helpers {

    enum class TaskSpecPreparationStatus {
        kPrepared,
        kCanceled,
        kFailed
    };

    struct TaskSpecPreparationResult {
        TaskSpecPreparationStatus status = TaskSpecPreparationStatus::kFailed;
        GuiTaskSpec spec;
    };

    uint32_t benchmark_iterations_or_default(GuiTaskSpec const& spec);
    QString task_title(GuiTaskSpec const& spec);

    TaskSpecPreparationResult prepare_task_spec_with_optional_dialog(GuiTaskSpec const& requested_spec);

#ifdef Z7_TESTING
    bool suppress_result_dialogs_for_tests();
#endif

    QString build_test_result_message(z7::app::OperationResult const& result, uint64_t archive_count_hint);
    void show_test_result_dialog(QWidget* parent, QString const& title, QString const& text);
    QVector<QPair<QString, QString>> hash_result_dialog_rows(GuiTaskRunResult const& run_result);

} // namespace z7::ui::gui::gui_app_controller_helpers
