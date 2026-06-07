// src/ui/filemanager/src/main_window/actions/action_tasks.cpp
// Role: Split/combine/options/benchmark and runner task orchestration.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "dialogs/temp_files/temp_files_dialog.h"

#include "archive_session_helpers.h"
#include "archive_failure_messages.h"

#include <QEventLoop>

#include <limits>
#include <memory>
#include <optional>

#include "task_background_mode.h"

namespace z7::ui::filemanager {

namespace {

std::optional<qulonglong> first_split_volume_size_bytes(
    const QString& volume_size_spec) {
  QVector<qulonglong> values;
  bool prev_is_number = false;
  const QString input = volume_size_spec.trimmed();

  for (qsizetype i = 0; i < input.size();) {
    const QChar c = input.at(i++);
    if (c.isSpace()) {
      continue;
    }
    if (c == QLatin1Char('-')) {
      break;
    }

    if (prev_is_number) {
      prev_is_number = false;
      unsigned num_bits = 0;
      switch (c.toLower().toLatin1()) {
        case 'b':
          continue;
        case 'k':
          num_bits = 10;
          break;
        case 'm':
          num_bits = 20;
          break;
        case 'g':
          num_bits = 30;
          break;
        case 't':
          num_bits = 40;
          break;
        default:
          break;
      }
      if (num_bits != 0) {
        qulonglong& value = values.back();
        if (value > (std::numeric_limits<qulonglong>::max() >> num_bits)) {
          return std::nullopt;
        }
        value <<= num_bits;
        while (i < input.size() && !input.at(i).isSpace()) {
          ++i;
        }
        continue;
      }
    }

    --i;
    const qsizetype start = i;
    qulonglong value = 0;
    while (i < input.size() && input.at(i).isDigit()) {
      const int digit = input.at(i).digitValue();
      if (value >
          (std::numeric_limits<qulonglong>::max() -
           static_cast<qulonglong>(digit)) /
              10ULL) {
        return std::nullopt;
      }
      value = value * 10ULL + static_cast<qulonglong>(digit);
      ++i;
    }
    if (i == start || value == 0) {
      return std::nullopt;
    }
    values.push_back(value);
    prev_is_number = true;
  }

  if (values.isEmpty()) {
    return std::nullopt;
  }
  return values.front();
}

}  // namespace

void MainWindow::on_split_requested() {
  if (in_archive_view()) {
    return;
  }

  const QStringList paths = active_panel_controller().selected_real_item_paths();
  if (paths.size() != 1 || !QFileInfo(paths.front()).isFile()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(549)),
                         z7::ui::runtime_support::L(3014));
    return;
  }

  QString default_output_dir = current_directory_for_panel(active_panel_index_);
  const int other_panel = 1 - active_panel_index_;
  if (two_panels_visible_ && !in_archive_view_for_panel(other_panel)) {
    const QString other_dir = current_directory_for_panel(other_panel);
    if (!other_dir.isEmpty()) {
      default_output_dir = other_dir;
    }
  }

  const QString output_dir = QFileDialog::getExistingDirectory(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(549)),
      default_output_dir,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (output_dir.isEmpty()) {
    return;
  }

  bool ok = false;
  const QString volume_size_spec = QInputDialog::getText(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(549)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7302)),
      QLineEdit::Normal,
      QStringLiteral("10M"),
      &ok);
  if (!ok || volume_size_spec.trimmed().isEmpty()) {
    return;
  }

  start_split_task(paths.front(), output_dir, volume_size_spec.trimmed());
}

void MainWindow::on_combine_requested() {
  if (in_archive_view()) {
    return;
  }

  const QStringList paths = active_panel_controller().selected_real_item_paths();
  if (paths.size() != 1 || !QFileInfo(paths.front()).isFile()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(550)),
                         z7::ui::runtime_support::L(7403));
    return;
  }

  QString default_output_dir = current_directory_for_panel(active_panel_index_);
  const int other_panel = 1 - active_panel_index_;
  if (two_panels_visible_ && !in_archive_view_for_panel(other_panel)) {
    const QString other_dir = current_directory_for_panel(other_panel);
    if (!other_dir.isEmpty()) {
      default_output_dir = other_dir;
    }
  }

  const QString output_dir = QFileDialog::getExistingDirectory(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(550)),
      default_output_dir,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (output_dir.isEmpty()) {
    return;
  }

  start_combine_task(paths.front(), output_dir);
}

void MainWindow::on_create_folder_requested() {
  if (in_archive_view()) {
    return;
  }
  bool ok = false;
  const QString name = QInputDialog::getText(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6300)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6302)),
      QLineEdit::Normal,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6304)),
      &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  QString normalized_name;
  QString error_message;
  if (!validate_basename_only_name(name, &normalized_name, &error_message)) {
    QMessageBox::warning(
        this,
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::L(6300)),
        error_message);
    return;
  }

  const QString parent_dir = current_directory_for_panel(active_panel_index_);
  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6300)), normalized_name),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6306)),
      [parent_dir, normalized_name](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_create_directory(parent_dir, normalized_name);
      },
      [this](bool ok_create,
             int,
             int,
             const QString&,
             const z7::app::OperationOutcome&) {
        if (ok_create) {
          refresh_directory();
        }
      });
}

void MainWindow::on_create_file_requested() {
  if (in_archive_view()) {
    return;
  }

  bool ok = false;
  const QString name = QInputDialog::getText(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6301)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6303)),
      QLineEdit::Normal,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6305)),
      &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  QString normalized_name;
  QString error_message;
  if (!validate_basename_only_name(name, &normalized_name, &error_message)) {
    QMessageBox::warning(
        this,
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6301)),
        error_message);
    return;
  }

  const QString parent_dir = current_directory_for_panel(active_panel_index_);
  start_task_with_runner(
      QStringLiteral("%1: %2").arg(
          z7::ui::runtime_support::strip_mnemonic(
              z7::ui::runtime_support::L(6301)),
          normalized_name),
      z7::ui::runtime_support::strip_mnemonic(
          z7::ui::runtime_support::L(6307)),
      [parent_dir, normalized_name](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_create_file(parent_dir, normalized_name);
      },
      [this](bool ok_create,
             int,
             int,
             const QString&,
             const z7::app::OperationOutcome&) {
        if (ok_create) {
          refresh_directory();
        }
      });
}

void MainWindow::on_options_requested() {
  OptionsDialog dialog(this);
  connect(&dialog, &OptionsDialog::settings_applied, this, [this, &dialog]() {
    load_runtime_settings();
    apply_runtime_settings();
    if (dialog.language_changed()) {
      z7::ui::runtime_support::OfficialLangCatalog::instance().reload_from_settings();
      retranslate_ui();
      refresh_action_states();
    }
  });

  const int result = dialog.exec();
  if (result == QDialog::Accepted) {
    load_runtime_settings();
    apply_runtime_settings();
  }
  refresh_action_states();
}

void MainWindow::on_benchmark_requested() {
  const QString caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(901));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kBenchmark;
  payload.refresh_after_finish = false;
  payload.benchmark = z7::task_ipc_runtime::TaskIpcBenchmarkPayload{};
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::on_benchmark2_requested() {
  const QString caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(901));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kBenchmark;
  payload.refresh_after_finish = false;
  payload.benchmark = z7::task_ipc_runtime::TaskIpcBenchmarkPayload{};
  payload.benchmark->method_value = QStringLiteral("*");
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::on_temp_files_requested() {
  const QString temp_path = QDir::cleanPath(QDir::tempPath());
  if (temp_path.isEmpty()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(910)),
                         QStringLiteral("Temporary directory is unavailable."));
    return;
  }

  const QFileInfo temp_info(temp_path);
  if (!temp_info.exists() || !temp_info.isDir()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(910)),
                         QStringLiteral("Temporary directory does not exist:\n%1")
                             .arg(QDir::toNativeSeparators(temp_path)));
    return;
  }

  TempFilesDialog dialog(temp_info.absoluteFilePath(), this);
  dialog.exec();
}

void MainWindow::on_contents_requested() {}

bool MainWindow::start_task_with_runner(const QString& header,
                                        const QString& failure_caption,
                                        const std::function<bool(ArchiveProcessRunner*)>& start_fn,
                                        const std::function<void(bool,
                                                                 int,
                                                                 int,
                                                                 const QString&,
                                                                 const z7::app::OperationOutcome&)>&
                                            finished_cb,
                                        RunnerTaskUiMode task_ui_mode,
                                        const std::function<bool(int, const QString&)>&
                                            should_show_failure) {
  auto* const created_runner = new ArchiveProcessRunner(this);
  TaskProgressDialog* const created_dialog =
      task_ui_mode == RunnerTaskUiMode::kSilent
          ? nullptr
          : new TaskProgressDialog(this);
  const auto progress_presenter =
      std::make_shared<z7::ui::runtime_support::DelayedProgressDialogPresenter>(
          created_dialog);
  const std::shared_ptr<RunningTaskContext> task =
      std::make_shared<RunningTaskContext>();
  const auto failure_messages = std::make_shared<QStringList>();
  task->runner = created_runner;
  task->dialog = created_dialog;
  active_runner_tasks_.push_back(task);

  const int origin_panel_index = active_panel_index_;
  QPointer<ArchiveProcessRunner> runner_guard(created_runner);
  QPointer<TaskProgressDialog> dialog_guard(created_dialog);
  const auto background_mode =
      std::make_shared<z7::ui::common::TaskBackgroundModeController>();
  const auto remove_task = [this, task]() {
    for (qsizetype i = 0; i < active_runner_tasks_.size(); ++i) {
      if (active_runner_tasks_[i].get() == task.get()) {
        active_runner_tasks_.removeAt(i);
        break;
      }
    }
  };

  created_runner->set_prompt_parent_provider([this]() -> QWidget* {
    return this;
  });

  connect(created_runner,
          &ArchiveProcessRunner::failure_message,
          this,
          [failure_messages, dialog_guard](const QString& message) {
            const QString trimmed = message.trimmed();
            if (trimmed.isEmpty()) {
              return;
            }
            failure_messages->push_back(trimmed);
            if (dialog_guard) {
              dialog_guard->append_failure_result_message(trimmed);
            }
          });

  if (created_dialog != nullptr) {
    created_dialog->set_header(header);
#ifdef Z7_TESTING
    QStringList task_headers =
        property("z7.fm.task.headers").toStringList();
    task_headers << header;
    setProperty("z7.fm.task.headers", task_headers);
#endif
    created_dialog->set_test_mode(false);
    created_runner->set_prompt_parent_provider(
        [dialog_guard, progress_presenter]() -> QWidget* {
          if (!dialog_guard) {
            return nullptr;
          }
          progress_presenter->show_now();
          dialog_guard->raise();
          dialog_guard->activateWindow();
          QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
          return dialog_guard.data();
        });
    connect(created_dialog,
            &TaskProgressDialog::cancel_requested,
            created_runner,
            &ArchiveProcessRunner::cancel);
    connect(created_dialog,
            &TaskProgressDialog::pause_requested,
            created_runner,
            &ArchiveProcessRunner::pause);
    connect(created_dialog,
            &TaskProgressDialog::resume_requested,
            created_runner,
            &ArchiveProcessRunner::resume);
    connect(created_dialog,
            &TaskProgressDialog::background_requested,
            this,
            [background_mode](bool backgrounded) {
              background_mode->set_backgrounded(backgrounded);
            });
    created_dialog->set_pause_available(created_runner->supports_pause());

    connect(created_runner, &ArchiveProcessRunner::started, created_dialog,
            [dialog_guard](const QString& backend,
                           const QString& operation,
                           const QStringList& targets) {
              if (!dialog_guard) {
                return;
              }
              dialog_guard->append_log(
                  QStringLiteral("%1 %2 %3")
                      .arg(backend, operation, targets.join(QLatin1Char(' '))));
            });

    connect(created_runner,
            &ArchiveProcessRunner::log_line,
            created_dialog,
            &TaskProgressDialog::append_log);

    connect(created_runner,
            &ArchiveProcessRunner::progress_changed,
            created_dialog,
            &TaskProgressDialog::set_percent);
    connect(created_runner,
            &ArchiveProcessRunner::detailed_progress_changed,
            created_dialog,
            [dialog_guard, runner_guard](bool totals_known,
                                         quint64 total_bytes,
                                         quint64 completed_bytes,
                                         quint64 total_files,
                                         quint64 completed_files,
                                         quint64 error_count,
                                         bool ratio_input_size_known,
                                         quint64 ratio_input_size,
                                         bool ratio_output_size_known,
                                         quint64 ratio_output_size,
                                         bool ratio_compressing_mode,
                                         const QString& current_path) {
              if (!dialog_guard) {
                return;
              }
              if (runner_guard) {
                dialog_guard->set_pause_available(runner_guard->supports_pause());
              }
              std::optional<z7::ui::runtime_support::TaskProgressRatioInfo> ratio_info;
              if (ratio_input_size_known || ratio_output_size_known) {
                z7::ui::runtime_support::TaskProgressRatioInfo ratio;
                ratio.input_size_known = ratio_input_size_known;
                ratio.input_size = ratio_input_size;
                ratio.output_size_known = ratio_output_size_known;
                ratio.output_size = ratio_output_size;
                ratio.compressing_mode = ratio_compressing_mode;
                ratio_info = ratio;
              }
              dialog_guard->set_detailed_progress(totals_known,
                                                  total_bytes,
                                                  completed_bytes,
                                                  total_files,
                                                  completed_files,
                                                  error_count,
                                                  ratio_info,
                                                  current_path);
            });

    connect(created_runner,
            &ArchiveProcessRunner::stage_changed,
            created_dialog,
            [dialog_guard, runner_guard](const QString& stage) {
              if (!dialog_guard) {
                return;
              }
              if (runner_guard) {
                dialog_guard->set_pause_available(runner_guard->supports_pause());
              }
              dialog_guard->set_stage(stage);
            });
  }

  connect(created_runner, &ArchiveProcessRunner::finished, this,
          [this,
           remove_task,
           created_runner,
           dialog_guard,
           progress_presenter,
           background_mode,
           failure_messages,
           failure_caption,
           finished_cb,
           should_show_failure,
           origin_panel_index](bool ok,
                                int exit_code,
                                int error_domain,
                                const QString& summary) {
            background_mode->restore();
            if (dialog_guard) {
              dialog_guard->set_running(false);
              dialog_guard->append_log(
                  QStringLiteral("%1 (exit=%2)").arg(summary).arg(exit_code));
            }
            show_transient_status_message(summary, 5000, origin_panel_index);

            const bool canceled =
                static_cast<z7::app::ArchiveErrorDomain>(error_domain) ==
                z7::app::ArchiveErrorDomain::kCanceled;

            QStringList result_messages = *failure_messages;
            const QString localized_summary =
                z7::ui::runtime_support::localize_archive_failure_message(
                    summary)
                    .trimmed();
            if (!ok && !canceled && result_messages.isEmpty()) {
              if (!localized_summary.isEmpty()) {
                result_messages.push_back(localized_summary);
              } else {
                result_messages.push_back(stable_error_message(error_domain));
              }
            }

            const bool keep_dialog_for_result =
                dialog_guard && !canceled && (!ok || !result_messages.isEmpty());
            if (keep_dialog_for_result) {
              dialog_guard->set_failure_result_messages(result_messages);
              dialog_guard->set_failure_result_mode();
              progress_presenter->show_now();
            }

            if (!dialog_guard && !ok && !canceled &&
                (!should_show_failure ||
                 should_show_failure(error_domain, summary))) {
              const QString failure_message =
                  !result_messages.isEmpty()
                      ? result_messages.join(QLatin1Char('\n'))
                      : (localized_summary.isEmpty()
                             ? stable_error_message(error_domain)
                             : localized_summary);
              QMessageBox::warning(this,
                                   failure_caption,
                                   failure_message);
            }

            const z7::app::OperationResult finished_result =
                created_runner != nullptr
                    ? created_runner->last_result()
                    : z7::app::OperationResult();
            const z7::app::OperationOutcome finished_outcome =
                created_runner != nullptr
                    ? created_runner->last_outcome()
                    : z7::app::archive_session_helpers::make_outcome(
                          finished_result,
                          z7::app::OperationPayload{std::monostate{}});
            remove_task();
            if (created_runner != nullptr) {
              created_runner->deleteLater();
            }

            if (finished_cb) {
              finished_cb(ok,
                          exit_code,
                          error_domain,
                          summary,
                          finished_outcome);
            }

            if (!dialog_guard) {
              return;
            }

            if (!keep_dialog_for_result) {
              progress_presenter->cancel_pending();
              dialog_guard->deleteLater();
            }
          });

  if (created_dialog != nullptr) {
    created_dialog->set_running(true);
    if (task_ui_mode == RunnerTaskUiMode::kDelayed) {
      progress_presenter->schedule(
          this, z7::ui::runtime_support::kOriginal7ZipProgressDialogDelayMs);
    } else {
      progress_presenter->show_now();
    }
  }

  // start_fn() now returns true whenever the runner has guaranteed a later
  // finished() delivery, including immediate validation/backend failures.
  const bool started = start_fn(created_runner);
  if (!started) {
    background_mode->restore();
    remove_task();
    if (created_dialog != nullptr) {
      progress_presenter->cancel_pending();
      created_dialog->set_running(false);
      created_dialog->deleteLater();
    }
    created_runner->deleteLater();
    return false;
  }
  return true;
}

void MainWindow::start_split_task(const QString& source_file_path,
                                  const QString& output_dir,
                                  const QString& volume_size_spec) {
  const QFileInfo source_info(source_file_path);
  const std::optional<qulonglong> first_volume_size =
      first_split_volume_size_bytes(volume_size_spec);
  if (first_volume_size.has_value() && source_info.exists() &&
      source_info.isFile() &&
      static_cast<qulonglong>(source_info.size()) <= *first_volume_size) {
    QMessageBox::warning(
        this,
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::L(549)),
        z7::ui::runtime_support::L(7306));
    return;
  }

  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(549)), source_file_path),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(549)),
      [source_file_path, output_dir, volume_size_spec](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_split(source_file_path, output_dir, volume_size_spec);
      },
      [this](bool ok, int, int, const QString&, const z7::app::OperationOutcome&) {
        if (ok) {
          refresh_directory();
        }
      });
}

}  // namespace z7::ui::filemanager
