// src/ui/gui/src/benchmark/layout.cpp
// Role: Benchmark dialog layout and event routing.
// This partition is intentionally kept under 1000 lines.

#include "benchmark_dialog.h"
#include "internal.h"
#include "../gui_task_runner_helpers.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSysInfo>
#include <QTimer>
#include <QTextOption>
#include <QVBoxLayout>

#include "archive_session.h"
#include "archive_error.h"
#include "app_startup_qt.h"
#include "archive_delegate_qt.h"
#include "custom_localization.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui {

using namespace benchmark_internal;

namespace {

class BenchmarkOperationDelegate final
    : public z7::ui::archive_support::OwnerRelayDelegate<BenchmarkDialog> {
 public:
  BenchmarkOperationDelegate(
      BenchmarkDialog* owner,
      std::function<void(const z7::app::OperationOutcome&)> finished_cb)
      : z7::ui::archive_support::OwnerRelayDelegate<BenchmarkDialog>(
            owner,
            std::move(finished_cb),
            nullptr,
            z7::ui::archive_support::MissingTargetPolicy::kDrop) {}

  // Explicit opt-out of OutcomeRelayDelegate's default on_lifecycle->on_log
  // relay: handle_operation_event dispatches on OperationEventKind, and
  // lifecycle events must stay on the lifecycle channel rather than being
  // synthesized as kLog. This override is a contract, not legacy scaffolding.
  void on_lifecycle(z7::app::OperationStage stage,
                    std::string_view message) override {
    Q_UNUSED(stage);
    Q_UNUSED(message);
  }

  void on_log(const z7::app::ArchiveLog& log) override {
    z7::app::OperationEvent event;
    event.kind = z7::app::OperationEventKind::kLog;
    event.stage = log.stage;
    event.output_channel = log.channel;
    event.message = log.message;
    event.benchmark_snapshot = log.benchmark_snapshot;
    event.benchmark_summary = log.benchmark_summary;
    post_to_owner([event = std::move(event)](BenchmarkDialog* owner) {
      owner->dispatch_operation_event(event);
    });
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    z7::app::OperationEvent event;
    event.kind = z7::app::OperationEventKind::kProgress;
    event.stage = progress.stage;
    event.message = progress.message;
    event.percent = progress.percent;
    event.totals_known = progress.totals_known;
    event.total_bytes = progress.total_bytes;
    event.completed_bytes = progress.completed_bytes;
    event.total_files = progress.total_files;
    event.completed_files = progress.completed_files;
    event.error_count = progress.error_count;
    event.current_path = progress.current_path;
    event.benchmark_snapshot = progress.benchmark_snapshot;
    event.benchmark_summary = progress.benchmark_summary;
    post_to_owner([event = std::move(event)](BenchmarkDialog* owner) {
      owner->dispatch_operation_event(event);
    });
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    BenchmarkDialog* dialog = owner();
    if (dialog == nullptr) {
      return std::nullopt;
    }
    return show_memory_limit_prompt_dialog(dialog, prompt);
  }
};

}  // namespace

int BenchmarkDialog::last_exit_code() const {
  return last_exit_code_;
}

void BenchmarkDialog::dispatch_operation_event(const z7::app::OperationEvent& event) {
  handle_operation_event(event);
}

BenchmarkCommandOptions BenchmarkDialog::options() const {
  BenchmarkCommandOptions out = initial_options_;
  out.total_mode = total_mode_ui_;

  out.iterations = selected_iterations();

  if (threads_combo_ != nullptr) {
    out.thread_count = z7::ui::archive_support::to_native_string(
        threads_combo_->currentData().toString().trimmed());
    if (out.thread_count.empty()) {
      out.thread_count = z7::ui::archive_support::to_native_string(
          threads_combo_->currentText().trimmed());
    }
  }

  if (dictionary_combo_ != nullptr) {
    out.dictionary_size = z7::ui::archive_support::to_native_string(
        dictionary_combo_->currentData().toString().trimmed());
    if (out.dictionary_size.empty()) {
      out.dictionary_size = z7::ui::archive_support::to_native_string(
          dictionary_combo_->currentText().trimmed());
    }
  }

  return out;
}

void BenchmarkDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  if (first_show_started_) {
    return;
  }
  first_show_started_ = true;
  QTimer::singleShot(0, this, &BenchmarkDialog::start_benchmark);
}

void BenchmarkDialog::reject() {
  if (benchmark_running_) {
    close_after_stop_ = true;
    stop_benchmark();
    return;
  }
  QDialog::reject();
}

void BenchmarkDialog::start_benchmark() {
  if (benchmark_running_) {
    return;
  }

  reset_result_view();
  reset_log_view();
  passed_count_ = 0;
  benchmark_finished_ = false;
  prev_dict_log_ = -1;
  current_pass_has_summary_ = false;
  update_passed_label();

  const BenchmarkCommandOptions current = options();
  z7::app::BenchmarkRequest request;
  request.iterations = current.iterations;
  request.thread_count = current.thread_count;
  request.dictionary_size = current.dictionary_size;
  request.method_value = current.method_value;
  request.total_mode = current.total_mode;

  if (!total_mode_ui_) {
    const z7::app::BenchmarkMemoryEstimate estimate =
        z7::app::estimate_benchmark_memory(request);
    if (estimate.ok && !estimate.within_limit) {
      const QString message =
          z7::ui::runtime_support::J(
              QStringLiteral("ui.benchmark.memory_limit_exceeded"));
      if (error_label_ != nullptr) {
        error_label_->setText(message);
        error_label_->setVisible(true);
      }
    }
  }

  set_running(true);
  elapsed_clock_.restart();
  update_elapsed_label();
  elapsed_timer_->start();

  const uint32_t run_iterations = std::max<uint32_t>(1, request.iterations);
  if (!total_mode_ui_) {
    request.iterations = 1;
  }

  pending_benchmark_request_ = request;
  pending_passes_ = total_mode_ui_ ? 1 : run_iterations;
  start_benchmark_pass(pending_benchmark_request_);
}

void BenchmarkDialog::stop_benchmark() {
  if (!benchmark_running_) {
    return;
  }
  if (stop_button_ != nullptr) {
    stop_button_->setEnabled(false);
  }
  active_task_.cancel();
}

void BenchmarkDialog::start_benchmark_pass(const z7::app::BenchmarkRequest& request) {
  auto delegate = std::make_shared<BenchmarkOperationDelegate>(
      this,
      [this](const z7::app::OperationOutcome& outcome) {
        on_benchmark_pass_finished(outcome);
      });
  active_delegate_ = delegate;
  active_task_ = engine_.start(z7::app::ArchiveRequest{request}, delegate);
  if (!active_task_.valid()) {
    active_delegate_.reset();
    on_benchmark_pass_finished(z7::app::make_backend_unavailable_outcome());
  }
}

void BenchmarkDialog::on_benchmark_pass_finished(const z7::app::OperationOutcome& outcome) {
  active_task_ = z7::app::ArchiveSession();
  active_delegate_.reset();

  const auto pass_result = z7::app::outcome_payload_as<z7::app::BenchmarkResult>(outcome);
  const bool pass_ok = pass_result.has_value() && pass_result->ok;
  if (!total_mode_ui_ && pass_ok) {
    ++passed_count_;
    update_passed_label();
    append_pass_row();
  }

  if (pending_passes_ > 0) {
    --pending_passes_;
  }
  if (!total_mode_ui_ && pass_ok && pending_passes_ > 0) {
    QTimer::singleShot(0, this, [this]() {
      start_benchmark_pass(pending_benchmark_request_);
    });
    return;
  }

  finalize_benchmark_run(outcome);
}

void BenchmarkDialog::finalize_benchmark_run(const z7::app::OperationOutcome& outcome) {
  const auto benchmark_result =
      z7::app::outcome_payload_as<z7::app::BenchmarkResult>(outcome);
  const z7::app::OperationResult result_base =
      z7::app::operation_result_from_outcome(outcome);
  if (benchmark_result.has_value() && benchmark_result->typed_summary.has_value()) {
    apply_typed_summary(*benchmark_result->typed_summary);
  }
  last_exit_code_ = result_base.native_exit_code;
  elapsed_timer_->stop();
  set_running(false);
  update_elapsed_label();

  const bool canceled = z7::app::is_operation_canceled(result_base.error);
  benchmark_finished_ = result_base.ok && !canceled;
  rebuild_log_view();

  if (!result_base.ok && !canceled) {
    const QString summary =
        z7::ui::archive_support::from_native_string(
            z7::app::describe_archive_error(result_base.error))
            .trimmed();
    if (!summary.isEmpty()) {
      if (error_label_ != nullptr) {
        error_label_->setText(summary);
        error_label_->setVisible(true);
      }
      QMessageBox::warning(this,
                           lang_or(7600),
                           summary);
    }
  } else if (error_label_ != nullptr) {
    error_label_->setVisible(false);
  }

  if (restart_requested_) {
    benchmark_finished_ = false;
    restart_requested_ = false;
    QTimer::singleShot(0, this, &BenchmarkDialog::start_benchmark);
    return;
  }

  if (close_after_stop_) {
    close_after_stop_ = false;
    QDialog::reject();
  }
}

void BenchmarkDialog::reset_result_view() {
  const QString dots = QStringLiteral("...");
  set_metrics(compress_current_size_,
              compress_current_speed_,
              compress_current_usage_,
              compress_current_rpu_,
              compress_current_rating_,
              dots, dots, dots, dots, dots);
  set_metrics(compress_result_size_,
              compress_result_speed_,
              compress_result_usage_,
              compress_result_rpu_,
              compress_result_rating_,
              dots, dots, dots, dots, dots);
  set_metrics(decompress_current_size_,
              decompress_current_speed_,
              decompress_current_usage_,
              decompress_current_rpu_,
              decompress_current_rating_,
              dots, dots, dots, dots, dots);
  set_metrics(decompress_result_size_,
              decompress_result_speed_,
              decompress_result_usage_,
              decompress_result_rpu_,
              decompress_result_rating_,
              dots, dots, dots, dots, dots);
  if (total_usage_label_ != nullptr) {
    total_usage_label_->setText(dots);
  }
  if (total_rpu_label_ != nullptr) {
    total_rpu_label_->setText(dots);
  }
  if (total_rating_label_ != nullptr) {
    total_rating_label_->setText(dots);
  }
  if (elapsed_label_ != nullptr) {
    elapsed_label_->setText(QStringLiteral("0 s"));
  }
  if (error_label_ != nullptr) {
    error_label_->clear();
    error_label_->setVisible(false);
  }
  refresh_memory_estimate();
}

void BenchmarkDialog::reset_log_view() {
  freq_lines_.clear();
  pass_rows_.clear();
  current_pass_compr_rating_.clear();
  current_pass_decompr_rating_.clear();
  current_pass_total_rating_.clear();
  current_pass_cpu_usage_.clear();
  current_pass_has_summary_ = false;
  sum_pass_compr_ = 0.0;
  sum_pass_decompr_ = 0.0;
  sum_pass_total_ = 0.0;
  sum_pass_cpu_ = 0.0;
  summed_pass_count_ = 0;

  if (freq_log_edit_ != nullptr) {
    freq_log_edit_->clear();
  }
  if (freq_log_label_ != nullptr) {
    freq_log_label_->clear();
  }
}

void BenchmarkDialog::set_running(bool running) {
  benchmark_running_ = running;
  if (stop_button_ != nullptr) {
    stop_button_->setEnabled(running);
  }
}

void BenchmarkDialog::append_log_line(const QString& line) {
  if (!total_mode_ui_ || freq_log_edit_ == nullptr) {
    return;
  }
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  freq_log_edit_->appendPlainText(trimmed);
}

void BenchmarkDialog::handle_operation_event(const z7::app::OperationEvent& event) {
  if (event.kind == z7::app::OperationEventKind::kLifecycle) {
    return;
  }

  if (event.benchmark_snapshot.has_value()) {
    apply_typed_snapshot(*event.benchmark_snapshot);
    if (event.benchmark_summary.has_value()) {
      apply_typed_summary(*event.benchmark_summary);
    }
  }

  const QString line =
      z7::ui::archive_support::from_native_string(event.message).trimmed();
  if (line.isEmpty()) {
    return;
  }

  append_log_line(line);
}

}  // namespace z7::ui::gui

// End of layout.cpp
