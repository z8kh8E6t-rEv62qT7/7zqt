// src/ui/gui/src/benchmark/metric.cpp
// Role: Benchmark metrics, table rows, and controls.
// This partition is intentionally kept under 1000 lines.

#include "benchmark_dialog.h"
#include "internal.h"

#include <QComboBox>
#include <QLabel>

#include "archive_session.h"
#include "custom_localization.h"

namespace z7::ui::gui {

using namespace benchmark_internal;

void BenchmarkDialog::apply_typed_snapshot(
    const z7::app::BenchmarkTypedSnapshot& snapshot) {
  if (total_mode_ui_ &&
      snapshot.kind != z7::app::BenchmarkSnapshotKind::kFrequency) {
    return;
  }

  switch (snapshot.kind) {
    case z7::app::BenchmarkSnapshotKind::kFrequency: {
      if (!total_mode_ui_) {
        const QString line =
            z7::ui::archive_support::from_native_string(snapshot.frequency_line)
                .trimmed();
        if (!line.isEmpty() && !freq_lines_.contains(line)) {
          freq_lines_.push_back(line);
          rebuild_log_view();
        }
      }
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kDictionaryPass: {
      if (total_mode_ui_) {
        return;
      }
      const QString compress_size_text =
          snapshot.metrics.has_compress_size
              ? bytes_to_display_size(snapshot.metrics.compress_size_bytes)
              : QStringLiteral("...");
      const QString decompress_size_text =
          snapshot.metrics.has_decompress_size
              ? bytes_to_display_size(snapshot.metrics.decompress_size_bytes)
              : QStringLiteral("...");
      set_metrics(compress_current_size_,
                  compress_current_speed_,
                  compress_current_usage_,
                  compress_current_rpu_,
                  compress_current_rating_,
                  compress_size_text,
                  QStringLiteral("%1 KB/s")
                      .arg(snapshot.metrics.compress_speed_kb_per_sec),
                  QStringLiteral("%1%")
                      .arg(snapshot.metrics.compress_cpu_usage_percent),
                  mips_to_gips(QString::number(snapshot.metrics.compress_rpu_mips)) +
                      QStringLiteral(" GIPS"),
                  mips_to_gips(QString::number(snapshot.metrics.compress_rating_mips)) +
                      QStringLiteral(" GIPS"));
      set_metrics(decompress_current_size_,
                  decompress_current_speed_,
                  decompress_current_usage_,
                  decompress_current_rpu_,
                  decompress_current_rating_,
                  decompress_size_text,
                  QStringLiteral("%1 KB/s")
                      .arg(snapshot.metrics.decompress_speed_kb_per_sec),
                  QStringLiteral("%1%")
                      .arg(snapshot.metrics.decompress_cpu_usage_percent),
                  mips_to_gips(QString::number(snapshot.metrics.decompress_rpu_mips)) +
                      QStringLiteral(" GIPS"),
                  mips_to_gips(QString::number(snapshot.metrics.decompress_rating_mips)) +
                      QStringLiteral(" GIPS"));
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kAveragePass: {
      if (total_mode_ui_) {
        return;
      }
      const QString compress_size_text =
          snapshot.metrics.has_compress_size
              ? bytes_to_display_size(snapshot.metrics.compress_size_bytes)
              : (compress_current_size_ == nullptr
                     ? QStringLiteral("...")
                     : compress_current_size_->text());
      const QString decompress_size_text =
          snapshot.metrics.has_decompress_size
              ? bytes_to_display_size(snapshot.metrics.decompress_size_bytes)
              : (decompress_current_size_ == nullptr
                     ? QStringLiteral("...")
                     : decompress_current_size_->text());
      set_metrics(compress_result_size_,
                  compress_result_speed_,
                  compress_result_usage_,
                  compress_result_rpu_,
                  compress_result_rating_,
                  compress_size_text,
                  QStringLiteral("%1 KB/s")
                      .arg(snapshot.metrics.compress_speed_kb_per_sec),
                  QStringLiteral("%1%")
                      .arg(snapshot.metrics.compress_cpu_usage_percent),
                  mips_to_gips(QString::number(snapshot.metrics.compress_rpu_mips)) +
                      QStringLiteral(" GIPS"),
                  mips_to_gips(QString::number(snapshot.metrics.compress_rating_mips)) +
                      QStringLiteral(" GIPS"));
      set_metrics(decompress_result_size_,
                  decompress_result_speed_,
                  decompress_result_usage_,
                  decompress_result_rpu_,
                  decompress_result_rating_,
                  decompress_size_text,
                  QStringLiteral("%1 KB/s")
                      .arg(snapshot.metrics.decompress_speed_kb_per_sec),
                  QStringLiteral("%1%")
                      .arg(snapshot.metrics.decompress_cpu_usage_percent),
                  mips_to_gips(QString::number(snapshot.metrics.decompress_rpu_mips)) +
                      QStringLiteral(" GIPS"),
                  mips_to_gips(QString::number(snapshot.metrics.decompress_rating_mips)) +
                      QStringLiteral(" GIPS"));

      current_pass_compr_rating_ =
          mips_to_gips(QString::number(snapshot.metrics.compress_rating_mips));
      current_pass_decompr_rating_ =
          mips_to_gips(QString::number(snapshot.metrics.decompress_rating_mips));
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kTotalRating: {
      if (total_usage_label_ != nullptr) {
        total_usage_label_->setText(
            QStringLiteral("%1%").arg(snapshot.total_cpu_usage_percent));
      }
      if (total_rpu_label_ != nullptr) {
        total_rpu_label_->setText(
            mips_to_gips(QString::number(snapshot.total_rpu_mips)) + QStringLiteral(" GIPS"));
      }
      if (total_rating_label_ != nullptr) {
        total_rating_label_->setText(
            mips_to_gips(QString::number(snapshot.total_rating_mips)) + QStringLiteral(" GIPS"));
      }
      current_pass_cpu_usage_ = QStringLiteral("%1%").arg(snapshot.total_cpu_usage_percent);
      current_pass_total_rating_ = mips_to_gips(QString::number(snapshot.total_rating_mips));
      current_pass_has_summary_ = true;
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kRamSize: {
      if (memory_label_ == nullptr) {
        return;
      }
      const QString value = QStringLiteral("%1 MB").arg(snapshot.ram_size_mb);
      const QString current = memory_label_->text();
      if (current.isEmpty() || current == QStringLiteral("...")) {
        memory_label_->setText(value);
      } else if (current.contains(QLatin1Char('/'))) {
        const QString usage = current.split(QLatin1Char('/')).front().trimmed();
        memory_label_->setText(QStringLiteral("%1 / %2").arg(usage, value));
      } else {
        memory_label_->setText(QStringLiteral("%1 / %2").arg(current, value));
      }
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kRamUsage: {
      if (memory_label_ == nullptr) {
        return;
      }
      const QString value = QStringLiteral("%1 MB").arg(snapshot.ram_usage_mb);
      const QString current = memory_label_->text();
      if (current.isEmpty() || current == QStringLiteral("...")) {
        memory_label_->setText(value);
      } else if (current.contains(QLatin1Char('/'))) {
        const QString total = current.split(QLatin1Char('/')).back().trimmed();
        memory_label_->setText(QStringLiteral("%1 / %2").arg(value, total));
      } else {
        memory_label_->setText(QStringLiteral("%1 / %2").arg(value, current));
      }
      return;
    }
    case z7::app::BenchmarkSnapshotKind::kUnknown:
      return;
  }
}

void BenchmarkDialog::apply_typed_summary(
    const z7::app::BenchmarkTypedSummary& summary) {
  if (summary.has_total_rating) {
    if (total_usage_label_ != nullptr) {
      total_usage_label_->setText(QStringLiteral("%1%").arg(summary.total_cpu_usage_percent));
    }
    if (total_rpu_label_ != nullptr) {
      total_rpu_label_->setText(
          mips_to_gips(QString::number(summary.total_rpu_mips)) + QStringLiteral(" GIPS"));
    }
    if (total_rating_label_ != nullptr) {
      total_rating_label_->setText(
          mips_to_gips(QString::number(summary.total_rating_mips)) + QStringLiteral(" GIPS"));
    }
  }
  if (memory_label_ != nullptr && summary.has_ram_usage && summary.has_ram_size) {
    memory_label_->setText(
        QStringLiteral("%1 MB / %2 MB").arg(summary.ram_usage_mb).arg(summary.ram_size_mb));
  }
}

void BenchmarkDialog::update_elapsed_label() {
  if (elapsed_label_ == nullptr) {
    return;
  }
  if (!elapsed_clock_.isValid()) {
    elapsed_label_->setText(QStringLiteral("0 s"));
    return;
  }
  elapsed_label_->setText(format_elapsed_text(elapsed_clock_.elapsed(),
                                              !benchmark_running_));
}

void BenchmarkDialog::update_passed_label() {
  if (passed_label_ == nullptr) {
    return;
  }
  passed_label_->setText(QStringLiteral("%1 /").arg(passed_count_));
}

void BenchmarkDialog::refresh_memory_estimate() {
  if (memory_label_ == nullptr || total_mode_ui_) {
    return;
  }

  z7::app::BenchmarkRequest request;
  request.iterations = selected_iterations();
  request.thread_count = threads_combo_ == nullptr
                             ? std::string("auto")
                             : z7::ui::archive_support::to_native_string(
                                   threads_combo_->currentData().toString().trimmed());
  request.dictionary_size = dictionary_combo_ == nullptr
                                ? std::string("32m")
                                : z7::ui::archive_support::to_native_string(
                                      dictionary_combo_->currentData().toString().trimmed());
  request.total_mode = false;

  const z7::app::BenchmarkMemoryEstimate estimate =
      z7::app::estimate_benchmark_memory(request);
  if (!estimate.ok) {
    memory_label_->setText(QStringLiteral("..."));
    return;
  }

  if (estimate.ram_defined) {
    memory_label_->setText(
        QStringLiteral("%1 / %2").arg(mb_text(estimate.estimated_usage_bytes),
                                     mb_text(estimate.total_ram_bytes)));
  } else {
    memory_label_->setText(mb_text(estimate.estimated_usage_bytes));
  }

  if (error_label_ != nullptr && !benchmark_running_) {
    if (!estimate.within_limit) {
      error_label_->setText(
          z7::ui::runtime_support::J(
              QStringLiteral("ui.benchmark.memory_limit_exceeded")));
      error_label_->setVisible(true);
    } else if (error_label_->text() ==
               z7::ui::runtime_support::J(
                   QStringLiteral("ui.benchmark.memory_limit_exceeded"))) {
      error_label_->clear();
      error_label_->setVisible(false);
    }
  }
}

void BenchmarkDialog::on_selector_changed() {
  if (suppress_auto_restart_) {
    return;
  }

  refresh_memory_estimate();
  if (!first_show_started_) {
    return;
  }
  if (!total_mode_ui_) {
    on_restart_clicked();
  }
}

uint32_t BenchmarkDialog::selected_iterations() const {
  if (iterations_combo_ == nullptr) {
    return initial_options_.iterations == 0 ? 1 : initial_options_.iterations;
  }

  bool ok = false;
  const int data_value = iterations_combo_->currentData().toInt(&ok);
  if (ok && data_value > 0) {
    return static_cast<uint32_t>(data_value);
  }

  return parse_uint32_or_default(iterations_combo_->currentText(), 1);
}

void BenchmarkDialog::append_pass_row() {
  if (total_mode_ui_ || !current_pass_has_summary_) {
    return;
  }

  const QString row = QStringLiteral("%1 %2 %3 %4")
                          .arg(current_pass_compr_rating_, -7)
                          .arg(current_pass_decompr_rating_, -7)
                          .arg(current_pass_total_rating_, -7)
                          .arg(current_pass_cpu_usage_, -5);
  pass_rows_.push_back(row.trimmed());
  sum_pass_compr_ += parse_double_or_zero(current_pass_compr_rating_);
  sum_pass_decompr_ += parse_double_or_zero(current_pass_decompr_rating_);
  sum_pass_total_ += parse_double_or_zero(current_pass_total_rating_);
  const QString cpu_number =
      current_pass_cpu_usage_.endsWith(QLatin1Char('%'))
          ? current_pass_cpu_usage_.left(current_pass_cpu_usage_.size() - 1)
          : current_pass_cpu_usage_;
  sum_pass_cpu_ += parse_double_or_zero(cpu_number);
  ++summed_pass_count_;
  current_pass_has_summary_ = false;
  rebuild_log_view();
}

void BenchmarkDialog::rebuild_log_view() {
  if (total_mode_ui_ || freq_log_label_ == nullptr) {
    return;
  }

  QStringList lines = freq_lines_;
  if (!pass_rows_.isEmpty()) {
    if (!lines.isEmpty()) {
      lines.push_back(QString());
    }
    lines.push_back(QStringLiteral("Compr Decompr Total   CPU"));
    lines.append(pass_rows_);
    if (benchmark_finished_ && summed_pass_count_ > 0) {
      lines.push_back(QStringLiteral("-------------"));
      const double inv = 1.0 / static_cast<double>(summed_pass_count_);
      const QString avg_row = QStringLiteral("%1 %2 %3 %4%")
                                  .arg(QString::number(sum_pass_compr_ * inv, 'f', 3), -7)
                                  .arg(QString::number(sum_pass_decompr_ * inv, 'f', 3), -7)
                                  .arg(QString::number(sum_pass_total_ * inv, 'f', 3), -7)
                                  .arg(QString::number(sum_pass_cpu_ * inv, 'f', 0), -5);
      lines.push_back(avg_row.trimmed());
    }
  }

  freq_log_label_->setText(lines.join(QLatin1Char('\n')));
}

void BenchmarkDialog::on_restart_clicked() {
  if (benchmark_running_) {
    restart_requested_ = true;
    stop_benchmark();
    return;
  }
  start_benchmark();
}

void BenchmarkDialog::on_stop_clicked() {
  stop_benchmark();
}

}  // namespace z7::ui::gui

// End of metric.cpp
