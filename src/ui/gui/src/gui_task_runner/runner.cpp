#include <QEventLoop>
#include <utility>

#include "../gui_task_runner_helpers.h"
#include "internal.h"

namespace z7::ui::gui {
    namespace {

        bool run_modal_via_specialized_spec_async(GuiTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
            if (auto const* export_spec = std::get_if<ArchiveExportTaskSpec>(&spec)) {
                run_archive_export_async_task(*export_spec, std::move(run_spec));
                return true;
            }
            if (auto const* hash = std::get_if<ArchiveHashTaskSpec>(&spec)) {
                run_archive_hash_async_task(*hash, std::move(run_spec));
                return true;
            }
            if (auto const* test = std::get_if<ArchiveTestTaskSpec>(&spec)) {
                run_archive_test_async_task(*test, std::move(run_spec));
                return true;
            }
            return false;
        }

        bool needs_specialized_async_runner(GuiTaskSpec const& spec) {
            return std::holds_alternative<ArchiveExportTaskSpec>(spec)
                || std::holds_alternative<ArchiveHashTaskSpec>(spec)
                || std::holds_alternative<ArchiveTestTaskSpec>(spec);
        }

    } // namespace

    bool task_is_test(GuiTaskSpec const& spec) {
        return std::holds_alternative<TestTaskSpec>(spec) || std::holds_alternative<ArchiveTestTaskSpec>(spec);
    }

    uint64_t test_archive_count_hint(GuiTaskSpec const& spec) {
        if (auto const* test_spec = std::get_if<TestTaskSpec>(&spec)) {
            return static_cast<uint64_t>(test_spec->archive_inputs.size());
        }
        return 1;
    }

    bool task_is_hash(GuiTaskSpec const& spec) {
        return std::holds_alternative<HashTaskSpec>(spec) || std::holds_alternative<ArchiveHashTaskSpec>(spec);
    }

    void GuiTaskRunner::run_modal_async(GuiTaskSpec const& spec,
                                        QString const& title,
                                        QWidget* parent,
                                        SharedTaskCancellation cancel_requested,
                                        FinishedCallback on_finished) {
        if (needs_specialized_async_runner(spec)) {
            if (run_modal_via_specialized_spec_async(spec,
                                                     make_sequenced_task_run_spec(title,
                                                                                  task_is_test(spec),
                                                                                  task_is_hash(spec),
                                                                                  parent,
                                                                                  nullptr,
                                                                                  std::move(cancel_requested),
                                                                                  std::move(on_finished)))) {
                return;
            }
        }

        GuiTaskRunResult out;
        z7::app::ArchiveRequest request;
        if (!build_archive_request(spec, &out, &request)) {
            if (on_finished) {
                on_finished(std::move(out));
            }
            return;
        }

        run_modal_async_with_request(spec,
                                     request,
                                     make_sequenced_task_run_spec(title,
                                                                  task_is_test(spec),
                                                                  task_is_hash(spec),
                                                                  parent,
                                                                  nullptr,
                                                                  std::move(cancel_requested),
                                                                  std::move(on_finished)));
    }

    GuiTaskRunResult
    GuiTaskRunner::run_modal_blocking_with_dialog(GuiTaskSpec const& spec,
                                                  QString const& title,
                                                  z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                                  SharedTaskCancellation cancel_requested) {
        GuiTaskRunResult out;
        if (dialog == nullptr) {
            out.result = z7::app::make_immediate_result(
                2, z7::app::ArchiveErrorDomain::kBackendUnavailable, "Task progress dialog is unavailable.");
            return out;
        }

        QEventLoop loop;
        bool finished = false;
        if (run_modal_via_specialized_spec_async(
                spec,
                make_sequenced_task_run_spec(title,
                                             task_is_test(spec),
                                             task_is_hash(spec),
                                             nullptr,
                                             dialog,
                                             std::move(cancel_requested),
                                             [&out, &finished, &loop](GuiTaskRunResult result) mutable {
                                                 out = std::move(result);
                                                 finished = true;
                                                 loop.quit();
                                             }))) {
            if (!finished) {
                loop.exec();
            }
            return out;
        }

        z7::app::ArchiveRequest request;
        if (!build_archive_request(spec, &out, &request)) {
            return out;
        }
        run_modal_async_with_request(
            spec,
            request,
            make_sequenced_task_run_spec(title,
                                         task_is_test(spec),
                                         task_is_hash(spec),
                                         nullptr,
                                         dialog,
                                         std::move(cancel_requested),
                                         [&out, &finished, &loop](GuiTaskRunResult result) mutable {
                                             out = std::move(result);
                                             finished = true;
                                             loop.quit();
                                         }));
        if (!finished) {
            loop.exec();
        }
        return out;
    }

} // namespace z7::ui::gui
