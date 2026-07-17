#include "gui_app_controller.h"

#include <QMessageBox>
#include <QStringList>
#include <utility>

#include "archive_error.h"
#include "archive_failure_messages.h"
#include "archive_format.h"
#include "archive_string_codec_qt.h"
#include "benchmark_dialog.h"
#include "gui_task_runner.h"
#include "task_result_presentation.h"
#include "gui_task_spec_ipc.h"
#include "hash_result_dialog.h"
#include "helpers.h"
#include "official_lang_catalog.h"
#include "task_ipc_runtime.h"

namespace z7::ui::gui {

    using namespace gui_app_controller_helpers;
    using z7::task_ipc_runtime::TaskIpcCommandKind;

    namespace {

        QString localized(uint32_t id) {
            return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id));
        }

        QString normalize_row_label(QString const& label) {
            QString normalized = label.trimmed();
            if (normalized.endsWith(QLatin1Char(':')) || normalized.endsWith(QChar(0xFF1A))) {
                normalized.chop(1);
            }
            return normalized.trimmed();
        }

        void append_hash_row(QVector<QPair<QString, QString>>* rows, QString const& label, QString const& value) {
            if (rows == nullptr) {
                return;
            }
            rows->append({normalize_row_label(label), value});
        }

        QVector<QPair<QString, QString>> build_hash_summary_rows(z7::app::HashSummary const& summary) {
            QVector<QPair<QString, QString>> rows;
            bool const single_file =
                summary.num_files == 1 && summary.num_dirs == 0 && !summary.first_file_name.empty();
            if (summary.num_errors != 0) {
                append_hash_row(&rows, localized(1070), QString::number(summary.num_errors));
            }

            if (single_file) {
                append_hash_row(
                    &rows, localized(1004), z7::ui::archive_support::from_native_string(summary.first_file_name));
            } else {
                if (!summary.main_name.empty()) {
                    append_hash_row(
                        &rows, localized(1004), z7::ui::archive_support::from_native_string(summary.main_name));
                }
                if (summary.num_dirs != 0) {
                    append_hash_row(&rows, localized(1031), QString::number(summary.num_dirs));
                }
                append_hash_row(&rows, localized(1032), QString::number(summary.num_files));
            }

            append_hash_row(&rows, localized(1007), z7::ui::archive_support::format_dual_size(summary.files_size));
            if (summary.num_alt_streams != 0) {
                append_hash_row(&rows, localized(1075), QString::number(summary.num_alt_streams));
                append_hash_row(
                    &rows, localized(1076), z7::ui::archive_support::format_dual_size(summary.alt_streams_size));
            }
            if (summary.physical_size_defined) {
                append_hash_row(
                    &rows, localized(1044), z7::ui::archive_support::format_dual_size(summary.physical_size));
            }

            auto const hash_label = [](uint32_t lang_id, QString const& method_name) {
                QString label = localized(lang_id);
                label.replace(QStringLiteral("CRC"), method_name);
                return label.trimmed();
            };

            for (z7::app::HashMethodDigest const& digest : summary.methods) {
                QString const method_name = z7::ui::archive_support::from_native_string(digest.method_name);
                if (single_file) {
                    if (digest.has_data_sum) {
                        append_hash_row(
                            &rows, method_name, z7::ui::archive_support::from_native_string(digest.data_sum));
                    }
                    continue;
                }
                if (digest.has_data_sum) {
                    append_hash_row(&rows,
                                    hash_label(7502, method_name),
                                    z7::ui::archive_support::from_native_string(digest.data_sum));
                }
                if (digest.has_names_sum) {
                    append_hash_row(&rows,
                                    hash_label(7503, method_name),
                                    z7::ui::archive_support::from_native_string(digest.names_sum));
                }
                if (summary.num_alt_streams != 0 && digest.has_streams_sum) {
                    append_hash_row(&rows,
                                    hash_label(7504, method_name),
                                    z7::ui::archive_support::from_native_string(digest.streams_sum));
                }
            }
            return rows;
        }

        QVector<QPair<QString, QString>> hash_result_dialog_rows_impl(GuiTaskRunResult const& run_result) {
            if (!run_result.result.hash_summary.has_value()) {
                return {};
            }
            return build_hash_summary_rows(*run_result.result.hash_summary);
        }

#ifdef Z7_TESTING
        QString hash_completion_summary_for_tests(GuiTaskRunResult const& run_result) {
            QStringList lines;
            QVector<QPair<QString, QString>> const rows = hash_result_dialog_rows_impl(run_result);
            for (auto const& row : rows) {
                lines.push_back(QStringLiteral("%1: %2").arg(row.first, row.second));
            }
            return lines.join(QLatin1Char('\n'));
        }
#endif

    } // namespace

    QVector<QPair<QString, QString>>
    gui_app_controller_helpers::hash_result_dialog_rows(GuiTaskRunResult const& run_result) {
        return hash_result_dialog_rows_impl(run_result);
    }

    void GuiAppController::run_task_ipc_payload_async(z7::task_ipc_runtime::TaskIpcPayload const& payload,
                                                      SharedTaskCancellation cancel_requested,
                                                      FinishedCallback on_finished) {
        if (payload.command == TaskIpcCommandKind::kOpen) {
            if (on_finished) {
                on_finished(GuiTaskCompletion{0, QString()});
            }
            return;
        }

        QString error;
        std::optional<GuiTaskSpec> const spec = build_task_spec_from_task_ipc_payload(payload, &error);
        if (!spec.has_value()) {
            if (!error.trimmed().isEmpty()
#ifdef Z7_TESTING
                && !suppress_result_dialogs_for_tests()
#endif
            ) {
                QMessageBox::critical(nullptr, QStringLiteral("7-Zip"), error);
            }
            if (on_finished) {
                GuiTaskCompletion completion;
                completion.exit_code = 7;
                completion.summary = error.trimmed();
                on_finished(completion);
            }
            return;
        }

        QString const title_override = payload.caption.trimmed();
        run_task_spec_async_impl(*spec, title_override, std::move(cancel_requested), std::move(on_finished));
    }

    void GuiAppController::run_task_spec_async(GuiTaskSpec const& spec,
                                               QString const& title_override,
                                               SharedTaskCancellation cancel_requested,
                                               FinishedCallback on_finished) {
        run_task_spec_async_impl(spec, title_override, std::move(cancel_requested), std::move(on_finished));
    }

    void GuiAppController::run_task_spec_async_impl(GuiTaskSpec const& requested_spec,
                                                    QString const& title_override,
                                                    SharedTaskCancellation cancel_requested,
                                                    FinishedCallback on_finished) {
        GuiTaskSpec const spec = requested_spec;
        auto finish = [callback = std::move(on_finished)](int code, QString const& summary = QString()) mutable {
            if (callback) {
                GuiTaskCompletion completion;
                completion.exit_code = code;
                completion.summary = summary.trimmed();
                callback(completion);
            }
        };

        if (auto const* benchmark_spec = std::get_if<BenchmarkTaskSpec>(&spec)) {
            BenchmarkCommandOptions options;
            options.iterations = benchmark_iterations_or_default(spec);
            options.thread_count = benchmark_spec->thread_count.empty() ? "auto" : benchmark_spec->thread_count;
            options.dictionary_size = benchmark_spec->dictionary_size.empty() ? "32m" : benchmark_spec->dictionary_size;
            options.method_value = benchmark_spec->method_value;
            options.total_mode = benchmark_spec->method_value == "*";

#ifdef Z7_TESTING
            if (benchmark_dialog_invoker_) {
                finish(benchmark_dialog_invoker_(options));
                return;
            }
#endif

            BenchmarkDialog dialog(options);
            QMetaObject::Connection remote_cancel_connection;
            if (cancel_requested) {
                if (cancel_requested->is_canceled()) {
                    static_cast<QDialog&>(dialog).reject();
                } else {
                    remote_cancel_connection = QObject::connect(
                        cancel_requested.data(),
                        &TaskCancellation::cancel_requested,
                        &dialog,
                        [&dialog]() { static_cast<QDialog&>(dialog).reject(); },
                        Qt::QueuedConnection);
                }
            }
            dialog.exec();
            if (remote_cancel_connection) {
                QObject::disconnect(remote_cancel_connection);
            }
            finish(dialog.last_exit_code());
            return;
        }

        TaskSpecPreparationResult const preparation = prepare_task_spec_with_optional_dialog(spec);
        if (preparation.status == TaskSpecPreparationStatus::kCanceled) {
            finish(5, QStringLiteral("Operation canceled."));
            return;
        }
        if (preparation.status == TaskSpecPreparationStatus::kFailed) {
            finish(255, QStringLiteral("Failed to prepare task request."));
            return;
        }
        GuiTaskSpec effective_spec = preparation.spec;

        auto on_task_finished = [finish = std::move(finish), spec](GuiTaskRunResult run_result) mutable {
            int exit_code = run_result.result.native_exit_code;
            bool const canceled = z7::app::is_operation_canceled(run_result.result.error);
            QString completion_summary;

#ifdef Z7_TESTING
            if ((std::holds_alternative<HashTaskSpec>(spec) || std::holds_alternative<ArchiveHashTaskSpec>(spec))
                && run_result.result.native_exit_code == 0
                && suppress_result_dialogs_for_tests()) {
                completion_summary = hash_completion_summary_for_tests(run_result);
            }
#endif

            z7::ui::runtime_support::TaskResultPresentation const presentation =
                z7::ui::runtime_support::classify_task_result(run_result.result.ok,
                                                              run_result.result.error.domain,
                                                              !run_result.result_messages.isEmpty());
            if (presentation == z7::ui::runtime_support::TaskResultPresentation::kFinalError
                || presentation == z7::ui::runtime_support::TaskResultPresentation::kFinalErrorThenMessages
                || canceled) {
                completion_summary = run_result.result.summary.empty()
                                       ? z7::ui::archive_support::from_native_string(
                                             z7::app::describe_archive_error(run_result.result.error))
                                       : z7::ui::archive_support::from_native_string(run_result.result.summary);
                if (!canceled
                    && !run_result.final_error_presented
                    && (presentation == z7::ui::runtime_support::TaskResultPresentation::kFinalError
                        || presentation
                            == z7::ui::runtime_support::TaskResultPresentation::kFinalErrorThenMessages)
                    && !completion_summary.isEmpty()
#ifdef Z7_TESTING
                    && !suppress_result_dialogs_for_tests()
#endif
                ) {
                    QMessageBox::critical(
                        nullptr,
                        QStringLiteral("7-Zip"),
                        z7::ui::runtime_support::localize_archive_failure_message(completion_summary));
                }
            }

            finish(exit_code, completion_summary);
        };

        GuiTaskRunner runner;
        runner.run_modal_async(effective_spec,
                               title_override.isEmpty() ? task_title(spec) : title_override,
                               nullptr,
                               std::move(cancel_requested),
                               std::move(on_task_finished));
    }

} // namespace z7::ui::gui
