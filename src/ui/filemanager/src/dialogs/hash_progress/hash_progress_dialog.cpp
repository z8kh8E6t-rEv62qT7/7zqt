// src/ui/filemanager/src/dialogs/hash_progress/hash_progress_dialog.cpp
// Role: Hash progress dialog metrics, controls, and title updates.

#include "hash_progress_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "archive_progress_format.h"
#include "official_lang_catalog.h"

namespace z7::ui::filemanager {

namespace {

QString lang(uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id));
}

QString label_with_colon(uint32_t id) {
  const QString text = lang(id);
  return text.endsWith(QLatin1Char(':')) ? text : text + QLatin1Char(':');
}

}  // namespace

using z7::ui::archive_support::format_hhmmss;
using z7::ui::archive_support::format_size_short;
using z7::ui::archive_support::format_speed;
using z7::ui::archive_support::now_msecs;

HashProgressDialog::HashProgressDialog(QWidget* parent)
    : QDialog(parent) {
#ifdef Z7_TESTING
  setObjectName(QStringLiteral("hashProgressDialog"));
#endif
  setWindowTitle(lang(7500));
  resize(920, 520);
  setModal(false);

  auto* layout = new QVBoxLayout(this);

  header_label_ = new QLabel(lang(7500), this);
  layout->addWidget(header_label_);

  auto* metrics_layout = new QGridLayout();
  metrics_layout->setHorizontalSpacing(18);
  metrics_layout->setVerticalSpacing(6);
  layout->addLayout(metrics_layout);

  int row = 0;
  metrics_layout->addWidget(new QLabel(lang(3900), this), row, 0);
  elapsed_value_ = new QLabel(QStringLiteral("00:00:00"), this);
  metrics_layout->addWidget(elapsed_value_, row, 1);
  metrics_layout->addWidget(new QLabel(lang(3902), this), row, 2);
  total_size_value_ = new QLabel(QStringLiteral("0 B"), this);
  metrics_layout->addWidget(total_size_value_, row, 3);

  ++row;
  metrics_layout->addWidget(new QLabel(lang(3901), this), row, 0);
  remaining_value_ = new QLabel(QString(), this);
  metrics_layout->addWidget(remaining_value_, row, 1);
  metrics_layout->addWidget(new QLabel(lang(3903), this), row, 2);
  speed_value_ = new QLabel(QString(), this);
  metrics_layout->addWidget(speed_value_, row, 3);

  ++row;
  metrics_layout->addWidget(new QLabel(label_with_colon(1032), this), row, 0);
  files_value_ = new QLabel(QStringLiteral("0 / 0"), this);
  metrics_layout->addWidget(files_value_, row, 1);
  metrics_layout->addWidget(new QLabel(lang(3904), this), row, 2);
  processed_value_ = new QLabel(QStringLiteral("0 B"), this);
  metrics_layout->addWidget(processed_value_, row, 3);

  ++row;
  metrics_layout->addWidget(new QLabel(lang(3906), this), row, 0);
  errors_value_ = new QLabel(QStringLiteral("0"), this);
  metrics_layout->addWidget(errors_value_, row, 1);

  current_dir_value_ = new QLabel(QString(), this);
  current_dir_value_->setWordWrap(true);
  layout->addWidget(current_dir_value_);

  current_file_value_ = new QLabel(QString(), this);
  current_file_value_->setWordWrap(true);
  layout->addWidget(current_file_value_);

  progress_bar_ = new QProgressBar(this);
  progress_bar_->setRange(0, 0);
  layout->addWidget(progress_bar_);

  auto* buttons_layout = new QHBoxLayout();
  layout->addLayout(buttons_layout);
  buttons_layout->addStretch(1);

  background_button_ = new QPushButton(lang(444), this);
#ifdef Z7_TESTING
  background_button_->setObjectName(QStringLiteral("hashProgressBackgroundButton"));
#endif
  buttons_layout->addWidget(background_button_);

  pause_button_ = new QPushButton(lang(446), this);
#ifdef Z7_TESTING
  pause_button_->setObjectName(QStringLiteral("hashProgressPauseButton"));
#endif
  buttons_layout->addWidget(pause_button_);

  cancel_button_ = new QPushButton(lang(402), this);
#ifdef Z7_TESTING
  cancel_button_->setObjectName(QStringLiteral("hashProgressCancelButton"));
#endif
  buttons_layout->addWidget(cancel_button_);

  refresh_timer_ = new QTimer(this);
  refresh_timer_->setInterval(250);
  connect(refresh_timer_, &QTimer::timeout, this, &HashProgressDialog::refresh_metrics);
  connect(background_button_, &QPushButton::clicked, this, [this]() {
    set_backgrounded(!backgrounded_);
    emit background_requested(backgrounded_);
  });
  connect(cancel_button_, &QPushButton::clicked, this, &HashProgressDialog::cancel_requested);
  connect(pause_button_, &QPushButton::clicked, this, [this]() {
    set_paused(!paused_);
    if (paused_) {
      emit pause_requested();
    } else {
      emit resume_requested();
    }
  });
}

void HashProgressDialog::set_operation_name(const QString& method_name) {
  operation_name_ = method_name;
  refresh_metrics();
}

void HashProgressDialog::set_stage(const QString& text) {
  stage_text_ = text.trimmed();
  refresh_metrics();
}

void HashProgressDialog::set_progress(bool totals_known,
                                      quint64 total_bytes,
                                      quint64 completed_bytes,
                                      quint64 total_files,
                                      quint64 completed_files,
                                      quint64 error_count,
                                      const QString& current_path) {
  totals_known_ = totals_known;
  total_bytes_ = total_bytes;
  completed_bytes_ = completed_bytes;
  total_files_ = total_files;
  completed_files_ = completed_files;
  error_count_ = error_count;
  current_path_ = current_path;
  update_current_path_labels();

  if (!totals_known_) {
    progress_bar_->setRange(0, 0);
  } else {
    const quint64 denom = total_bytes_ != 0 ? total_bytes_ : total_files_;
    const quint64 numer = total_bytes_ != 0 ? completed_bytes_ : completed_files_;
    if (denom == 0) {
      progress_bar_->setRange(0, 1);
      progress_bar_->setValue(1);
    } else {
      progress_bar_->setRange(0, 1000);
      const quint64 value = std::min<quint64>(1000, (numer * 1000) / denom);
      progress_bar_->setValue(static_cast<int>(value));
    }
  }

  refresh_metrics();
}

void HashProgressDialog::set_running(bool running) {
  running_ = running;
  if (running_) {
    started_ms_ = now_msecs();
    elapsed_ms_ = 0;
    paused_started_ms_ = -1;
    paused_total_ms_ = 0;
    error_count_ = 0;
    set_paused(false);
    set_backgrounded(false);
    background_button_->setEnabled(true);
    pause_button_->setEnabled(true);
    cancel_button_->setEnabled(true);
    refresh_timer_->start();
  } else {
    refresh_timer_->stop();
    if (paused_) {
      set_paused(false);
    }
    background_button_->setEnabled(false);
    pause_button_->setEnabled(false);
    cancel_button_->setEnabled(false);
  }
  refresh_metrics();
}

bool HashProgressDialog::is_paused() const {
  return paused_;
}

bool HashProgressDialog::is_backgrounded() const {
  return backgrounded_;
}

void HashProgressDialog::set_paused(bool paused) {
  if (paused_ == paused) {
    return;
  }
  const qint64 now = now_msecs();
  if (paused) {
    paused_started_ms_ = now;
  } else if (paused_started_ms_ >= 0) {
    paused_total_ms_ += now - paused_started_ms_;
    paused_started_ms_ = -1;
  }
  paused_ = paused;
  pause_button_->setText(paused_ ? lang(411) : lang(446));
  update_title();
}

void HashProgressDialog::set_backgrounded(bool backgrounded) {
  if (backgrounded_ == backgrounded) {
    return;
  }
  backgrounded_ = backgrounded;
  background_button_->setText(backgrounded_ ? lang(445)
                                            : lang(444));
  update_title();
}

void HashProgressDialog::refresh_metrics() {
  if (started_ms_ >= 0) {
    const qint64 now = now_msecs();
    qint64 paused_ms = paused_total_ms_;
    if (paused_ && paused_started_ms_ >= 0) {
      paused_ms += now - paused_started_ms_;
    }
    elapsed_ms_ = std::max<qint64>(0, now - started_ms_ - paused_ms);
  }

  const quint64 elapsed_sec = static_cast<quint64>(elapsed_ms_ / 1000);
  elapsed_value_->setText(format_hhmmss(elapsed_sec));

  const quint64 remaining_num = total_bytes_ != 0 ? total_bytes_ : total_files_;
  const quint64 remaining_den = total_bytes_ != 0 ? completed_bytes_ : completed_files_;
  if (totals_known_ && remaining_num != 0 && remaining_den != 0 &&
      remaining_den < remaining_num) {
    const quint64 remaining_ms = (static_cast<quint64>(elapsed_ms_) *
                                  (remaining_num - remaining_den)) /
                                 remaining_den;
    remaining_value_->setText(format_hhmmss(remaining_ms / 1000));
  } else {
    remaining_value_->clear();
  }

  if (total_files_ != 0) {
    files_value_->setText(
        QStringLiteral("%1 / %2").arg(completed_files_).arg(total_files_));
  } else {
    files_value_->setText(QString::number(completed_files_));
  }
  errors_value_->setText(QString::number(error_count_));

  total_size_value_->setText(format_size_short(total_bytes_));
  processed_value_->setText(format_size_short(completed_bytes_));
  speed_value_->setText(format_speed(completed_bytes_, elapsed_ms_));

  QString header;
  if (!stage_text_.isEmpty()) {
    header = stage_text_;
  } else {
    header = lang(7500);
  }
  if (!operation_name_.isEmpty()) {
    header += QStringLiteral(" (%1)").arg(operation_name_);
  }
  if (paused_) {
    header += QStringLiteral(" [%1]").arg(lang(447));
  }
  if (backgrounded_) {
    header += QStringLiteral(" [Backgrounded]");
  }
  header_label_->setText(header);
  update_title();
}

void HashProgressDialog::update_title() {
  QString title;
  if (totals_known_) {
    const quint64 denom = total_bytes_ != 0 ? total_bytes_ : total_files_;
    const quint64 numer = total_bytes_ != 0 ? completed_bytes_ : completed_files_;
    if (denom != 0) {
      const quint64 percent = std::min<quint64>(100, (numer * 100) / denom);
      title = QStringLiteral("%1% ").arg(percent);
    }
  }
  if (paused_) {
    title += QStringLiteral("%1 ").arg(lang(447));
  }
  if (backgrounded_) {
    title += QStringLiteral("Backgrounded ");
  }
  if (!stage_text_.isEmpty()) {
    title += stage_text_;
  } else {
    title += lang(7500);
  }
  setWindowTitle(title);
}

void HashProgressDialog::update_current_path_labels() {
  if (current_path_.isEmpty()) {
    current_dir_value_->clear();
    current_file_value_->clear();
    return;
  }

  const QString native = QDir::toNativeSeparators(current_path_);
  const QFileInfo info(native);
  if (!info.fileName().isEmpty()) {
    current_dir_value_->setText(info.absolutePath());
    current_file_value_->setText(info.fileName());
    return;
  }

  current_dir_value_->setText(native);
  current_file_value_->clear();
}

}  // namespace z7::ui::filemanager
