// src/ui/gui/src/gui_app_controller/helpers.h
// Role: Internal helper routines for GuiAppController command processing.

#pragma once

#include <cstdint>
#include <string>

#include <QString>

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

uint32_t benchmark_iterations_or_default(const GuiTaskSpec& spec);
QString task_title(const GuiTaskSpec& spec);

TaskSpecPreparationResult prepare_task_spec_with_optional_dialog(
    const GuiTaskSpec& requested_spec);

#ifdef Z7_TESTING
bool suppress_result_dialogs_for_tests();
#endif

QString build_test_result_message(const z7::app::OperationResult& result,
                                  uint64_t archive_count_hint);
void show_test_result_dialog(QWidget* parent,
                             const QString& title,
                             const QString& text);
QVector<QPair<QString, QString>> hash_result_dialog_rows(
    const GuiTaskRunResult& run_result);

}  // namespace z7::ui::gui::gui_app_controller_helpers
