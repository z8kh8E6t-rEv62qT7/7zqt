// src/ui/gui/src/gui_task_runner/internal.h
// Role: Private declarations shared by GuiTaskRunner compile units.

#pragma once

#include <QString>
#include <utility>

#include "archive_session.h"
#include "gui_task_runner.h"
#include "gui_task_spec.h"
#include "task_progress_dialog_base.h"

namespace z7::ui::gui {

    struct SequencedTaskRunSpec {
        QString title;
        bool test_mode = false;
        bool hash_mode = false;
        QString redundant_test_archive_context;
        QWidget* parent = nullptr;
        z7::ui::runtime_support::TaskProgressDialogBase* dialog = nullptr;
        SharedTaskCancellation cancel_requested;
        GuiTaskRunner::FinishedCallback on_finished;
    };

    inline SequencedTaskRunSpec make_sequenced_task_run_spec(QString title,
                                                             bool test_mode,
                                                             bool hash_mode,
                                                             QWidget* parent,
                                                             z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                                             SharedTaskCancellation cancel_requested,
                                                             GuiTaskRunner::FinishedCallback on_finished) {
        SequencedTaskRunSpec spec;
        spec.title = std::move(title);
        spec.test_mode = test_mode;
        spec.hash_mode = hash_mode;
        spec.parent = parent;
        spec.dialog = dialog;
        spec.cancel_requested = std::move(cancel_requested);
        spec.on_finished = std::move(on_finished);
        return spec;
    }

    void run_modal_async_with_request(GuiTaskSpec const& spec,
                                      z7::app::ArchiveRequest const& request,
                                      SequencedTaskRunSpec run_spec);
    void run_archive_export_async_task(ArchiveExportTaskSpec const& spec, SequencedTaskRunSpec run_spec);
    void run_archive_hash_async_task(ArchiveHashTaskSpec const& spec, SequencedTaskRunSpec run_spec);
    void run_archive_test_async_task(ArchiveTestTaskSpec const& spec, SequencedTaskRunSpec run_spec);

} // namespace z7::ui::gui
