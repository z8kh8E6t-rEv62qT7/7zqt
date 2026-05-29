// src/ui/gui/src/benchmark/layout_build.cpp
// Role: BenchmarkDialog UI construction and startup wiring.

#include "benchmark_dialog.h"
#include "internal.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "archive_session.h"
#include "archive_error.h"
#include "app_startup_qt.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui {
namespace {

}  // namespace

using namespace benchmark_internal;
using z7::ui::runtime_support::L;

BenchmarkDialog::BenchmarkDialog(const BenchmarkCommandOptions& initial,
                                 QWidget* parent)
    : QDialog(parent), initial_options_(initial) {
  total_mode_ui_ = initial.total_mode;
  setWindowTitle(lang_or(7600));
  resize(total_mode_ui_ ? 980 : 1180, total_mode_ui_ ? 700 : 760);

  auto* root = new QVBoxLayout(this);
  const z7::app::BenchmarkSystemInfo sys = z7::app::query_benchmark_system_info();

  if (total_mode_ui_) {
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(lang_or(3900), this));
    elapsed_label_ = new QLabel(QStringLiteral("0 s"), this);
    top->addWidget(elapsed_label_);
    top->addStretch();
    root->addLayout(top);

    freq_log_edit_ = new QPlainTextEdit(this);
    freq_log_edit_->setReadOnly(true);
#ifdef Z7_TESTING
    freq_log_edit_->setObjectName(QStringLiteral("benchmarkLogView"));
#endif
    apply_log_view_style(freq_log_edit_);
    root->addWidget(freq_log_edit_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel,
                                         Qt::Horizontal,
                                         this);
    auto* help_button = buttons->addButton(QDialogButtonBox::Help);
#ifdef Z7_TESTING
    help_button->setObjectName(QStringLiteral("benchmarkHelpButton"));
#endif
    help_button->setText(L(409));
    help_button->setEnabled(false);
    if (QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel)) {
      cancel_button->setText(L(402));
    }
    z7::platform::qt::apply_dialog_button_baseline(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
  } else {
    auto* main_row = new QHBoxLayout();
    auto* left_col = new QVBoxLayout();
    auto* right_col = new QVBoxLayout();
    left_col->setContentsMargins(0, 0, 0, 0);
    left_col->setSpacing(4);
    right_col->setContentsMargins(0, 0, 0, 0);
    main_row->addLayout(left_col, 332);
    main_row->addSpacing(8);
    main_row->addLayout(right_col, 140);
    root->addLayout(main_row, 1);

    auto* top_grid = new QGridLayout();
    top_grid->setContentsMargins(0, 0, 0, 0);
    top_grid->setHorizontalSpacing(8);
    top_grid->setVerticalSpacing(2);

    dictionary_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
    dictionary_combo_->setObjectName(QStringLiteral("benchmarkDictionaryCombo"));
#endif
    dictionary_combo_->setEditable(true);

    const uint64_t initial_dict_bytes =
        parse_size_to_bytes_or_default(z7::ui::archive_support::from_native_string(initial.dictionary_size), kDefaultDictBytes);
    int dict_cur = 0;
    for (uint32_t i = (kMinDicLogSize - 1) * 2; i <= (32 - 1) * 2; ++i) {
      const uint64_t dict = (static_cast<uint64_t>(2 + (i & 1)) << (i / 2));
      const int index = dictionary_combo_->count();
      dictionary_combo_->addItem(bytes_to_display_size(dict), bytes_to_switch_size(dict));
      dictionary_combo_->setItemData(index,
                                     QVariant::fromValue<qulonglong>(dict),
                                     Qt::UserRole + 1);
      if (dict <= initial_dict_bytes) {
        dict_cur = index;
      }
      if (dict >= (1ULL << 32)) {
        break;
      }
    }
    dictionary_combo_->setCurrentIndex(dict_cur);

    memory_label_ = new QLabel(QStringLiteral("..."), this);
#ifdef Z7_TESTING
    memory_label_->setObjectName(QStringLiteral("benchmarkMemoryLabel"));
#endif

    threads_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
    threads_combo_->setObjectName(QStringLiteral("benchmarkThreadsCombo"));
#endif
    threads_combo_->setEditable(false);

    const uint32_t cpu_threads = std::max<uint32_t>(1, sys.process_threads);
    uint32_t num_threads = cpu_threads;
    const QString initial_thread_count =
        z7::ui::archive_support::from_native_string(initial.thread_count).trimmed();
    if (!initial_thread_count.isEmpty() &&
        initial_thread_count.toLower() != QStringLiteral("auto")) {
      num_threads = parse_uint32_or_default(initial_thread_count, cpu_threads);
    }
    num_threads &= ~1U;
    if (num_threads == 0) {
      num_threads = 1;
    }
    num_threads = std::min<uint32_t>(num_threads, 1U << 14);

    const uint32_t num_threads_combo =
        std::max<uint32_t>(1, std::max<uint32_t>(sys.system_threads, cpu_threads) * 2);
    uint32_t v = 1;
    int cur = 0;
    for (; v <= num_threads_combo;) {
      int index = threads_combo_->count();
      threads_combo_->addItem(QString::number(v), QString::number(v));

      const uint32_t v_next = v + (v < 2 ? 1 : 2);
      if (v <= num_threads &&
          (num_threads < v_next || v_next > num_threads_combo)) {
        if (v != num_threads) {
          index = threads_combo_->count();
          threads_combo_->addItem(QString::number(num_threads),
                                  QString::number(num_threads));
        }
        cur = index;
      }
      v = v_next;
    }
    threads_combo_->setCurrentIndex(cur);

    const QString hardware_threads =
        z7::ui::archive_support::from_native_string(sys.hardware_threads).trimmed();
    hardware_threads_label_ =
        new QLabel(hardware_threads.isEmpty()
                       ? QStringLiteral("/ %1").arg(cpu_threads)
                       : hardware_threads,
                   this);
    hardware_threads_label_->setWordWrap(false);
    hardware_threads_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* threads_row = new QWidget(this);
    auto* threads_layout = new QHBoxLayout(threads_row);
    threads_layout->setContentsMargins(0, 0, 0, 0);
    threads_layout->setSpacing(6);
    threads_layout->addWidget(threads_combo_);
    threads_layout->addWidget(hardware_threads_label_);
    threads_row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    restart_button_ = new QPushButton(lang_or(443), this);
    stop_button_ = new QPushButton(lang_or(442), this);
    restart_button_->setMinimumWidth(200);
    stop_button_->setMinimumWidth(200);
    restart_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    stop_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* dictionary_label = new QLabel(lang_or(4006), this);
    auto* threads_label = new QLabel(lang_or(4009), this);
    auto* memory_caption = new QLabel(lang_or(7601), this);
    memory_caption->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    memory_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    top_grid->addWidget(dictionary_label, 0, 0);
    top_grid->addWidget(dictionary_combo_, 0, 1);
    top_grid->addWidget(memory_caption, 0, 2);
    top_grid->addWidget(restart_button_, 0, 3);
    top_grid->addWidget(threads_label, 1, 0);
    top_grid->addWidget(threads_row, 1, 1);
    top_grid->addWidget(memory_label_, 1, 2);
    top_grid->addWidget(stop_button_, 1, 3);
    top_grid->setColumnStretch(1, 1);
    top_grid->setColumnStretch(2, 1);
    left_col->addLayout(top_grid);

    freq_log_label_ = new QLabel(this);
#ifdef Z7_TESTING
    freq_log_label_->setObjectName(QStringLiteral("benchmarkRightLogLabel"));
#endif
    freq_log_label_->setMinimumWidth(340);
    freq_log_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    apply_log_label_style(freq_log_label_);
    right_col->addWidget(freq_log_label_, 1);

    auto* compress_group = new QGroupBox(lang_or(7602), this);
#ifdef Z7_TESTING
    compress_group->setObjectName(QStringLiteral("benchmarkCompressGroup"));
#endif
    auto* compress_grid = new QGridLayout(compress_group);
    add_metric_header(compress_grid, 0);

    compress_grid->addWidget(new QLabel(lang_or(7606), compress_group),
                             1, 0);
    compress_current_size_ = make_metric_label(compress_group);
    compress_current_speed_ = make_metric_label(compress_group);
    compress_current_usage_ = make_metric_label(compress_group);
    compress_current_rpu_ = make_metric_label(compress_group);
    compress_current_rating_ = make_metric_label(compress_group);
    compress_grid->addWidget(compress_current_size_, 1, 1);
    compress_grid->addWidget(compress_current_speed_, 1, 2);
    compress_grid->addWidget(compress_current_usage_, 1, 3);
    compress_grid->addWidget(compress_current_rpu_, 1, 4);
    compress_grid->addWidget(compress_current_rating_, 1, 5);

    compress_grid->addWidget(new QLabel(lang_or(7607),
                                        compress_group),
                             2, 0);
    compress_result_size_ = make_metric_label(compress_group);
    compress_result_speed_ = make_metric_label(compress_group);
    compress_result_usage_ = make_metric_label(compress_group);
    compress_result_rpu_ = make_metric_label(compress_group);
    compress_result_rating_ = make_metric_label(compress_group);
    compress_grid->addWidget(compress_result_size_, 2, 1);
    compress_grid->addWidget(compress_result_speed_, 2, 2);
    compress_grid->addWidget(compress_result_usage_, 2, 3);
    compress_grid->addWidget(compress_result_rpu_, 2, 4);
    compress_grid->addWidget(compress_result_rating_, 2, 5);

    auto* decompress_group = new QGroupBox(lang_or(7603), this);
#ifdef Z7_TESTING
    decompress_group->setObjectName(QStringLiteral("benchmarkDecompressGroup"));
#endif
    auto* decompress_grid = new QGridLayout(decompress_group);
    add_metric_header(decompress_grid, 0);

    decompress_grid->addWidget(new QLabel(lang_or(7606),
                                          decompress_group),
                               1, 0);
    decompress_current_size_ = make_metric_label(decompress_group);
    decompress_current_speed_ = make_metric_label(decompress_group);
    decompress_current_usage_ = make_metric_label(decompress_group);
    decompress_current_rpu_ = make_metric_label(decompress_group);
    decompress_current_rating_ = make_metric_label(decompress_group);
    decompress_grid->addWidget(decompress_current_size_, 1, 1);
    decompress_grid->addWidget(decompress_current_speed_, 1, 2);
    decompress_grid->addWidget(decompress_current_usage_, 1, 3);
    decompress_grid->addWidget(decompress_current_rpu_, 1, 4);
    decompress_grid->addWidget(decompress_current_rating_, 1, 5);

    decompress_grid->addWidget(new QLabel(lang_or(7607),
                                          decompress_group),
                               2, 0);
    decompress_result_size_ = make_metric_label(decompress_group);
    decompress_result_speed_ = make_metric_label(decompress_group);
    decompress_result_usage_ = make_metric_label(decompress_group);
    decompress_result_rpu_ = make_metric_label(decompress_group);
    decompress_result_rating_ = make_metric_label(decompress_group);
    decompress_grid->addWidget(decompress_result_size_, 2, 1);
    decompress_grid->addWidget(decompress_result_speed_, 2, 2);
    decompress_grid->addWidget(decompress_result_usage_, 2, 3);
    decompress_grid->addWidget(decompress_result_rpu_, 2, 4);
    decompress_grid->addWidget(decompress_result_rating_, 2, 5);

    left_col->addWidget(compress_group);
    left_col->addWidget(decompress_group);

    error_label_ = new QLabel(this);
#ifdef Z7_TESTING
    error_label_->setObjectName(QStringLiteral("benchmarkErrorLabel"));
#endif
    error_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    error_label_->setVisible(false);
    left_col->addWidget(error_label_);

    auto* status_row = new QHBoxLayout();
    elapsed_label_ = new QLabel(QStringLiteral("0 s"), this);
    passed_label_ = new QLabel(QStringLiteral("0 /"), this);
    status_row->addWidget(new QLabel(lang_or(3900), this));
    status_row->addWidget(elapsed_label_);
    status_row->addSpacing(24);
    status_row->addWidget(new QLabel(lang_or(7610), this));
    status_row->addWidget(passed_label_);
    status_row->addStretch();
    left_col->addLayout(status_row);

    iterations_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
    iterations_combo_->setObjectName(QStringLiteral("benchmarkIterationsCombo"));
#endif
    iterations_combo_->setEditable(false);

    uint32_t target_iterations = initial.iterations == 0 ? 10 : initial.iterations;
    int iter_index = -1;
    for (uint32_t iter = 1;;) {
      const int index = iterations_combo_->count();
      iterations_combo_->addItem(QString::number(iter), static_cast<int>(iter));
      if (iter == target_iterations) {
        iter_index = index;
      }

      const bool is_last = iter >= 10000000;
      if (is_last) {
        break;
      }

      uint32_t next = iter * 10;
      if (iter < 2) {
        next = 2;
      } else if (iter < 5) {
        next = 5;
      } else if (iter < 10) {
        next = 10;
      }
      iter = next;
    }
    if (iter_index < 0) {
      iter_index = iterations_combo_->count();
      iterations_combo_->addItem(QString::number(target_iterations),
                                 static_cast<int>(target_iterations));
    }
    iterations_combo_->setCurrentIndex(iter_index);

    auto* pass_picker = new QHBoxLayout();
    pass_picker->addWidget(iterations_combo_);
    pass_picker->addStretch();
    left_col->addLayout(pass_picker);

    auto* total_group = new QGroupBox(lang_or(7605), this);
    auto* total_grid = new QGridLayout(total_group);
    total_grid->addWidget(new QLabel(lang_or(7608), total_group), 0, 0);
    total_grid->addWidget(new QLabel(lang_or(7609), total_group), 0, 1);
    total_grid->addWidget(new QLabel(lang_or(7604), total_group), 0, 2);
    total_usage_label_ = make_metric_label(total_group);
    total_rpu_label_ = make_metric_label(total_group);
    total_rating_label_ = make_metric_label(total_group);
    total_grid->addWidget(total_usage_label_, 1, 0);
    total_grid->addWidget(total_rpu_label_, 1, 1);
    total_grid->addWidget(total_rating_label_, 1, 2);
    left_col->addWidget(total_group);

    auto* cpu_label = new QLabel(
        z7::ui::archive_support::from_native_string(sys.cpu).trimmed(), this);
    cpu_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    left_col->addWidget(cpu_label);

    auto* feature_row = new QHBoxLayout();
    auto* feature_label = new QLabel(
        z7::ui::archive_support::from_native_string(sys.cpu_feature).trimmed(), this);
    auto* ver_label = new QLabel(
        z7::ui::archive_support::from_native_string(sys.version).trimmed(), this);
    ver_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    feature_row->addWidget(feature_label, 1);
    feature_row->addWidget(ver_label, 0);
    left_col->addLayout(feature_row);

    if (!sys.sys1.empty()) {
      left_col->addWidget(new QLabel(
          z7::ui::archive_support::from_native_string(sys.sys1).trimmed(), this));
    }
    if (!sys.sys2.empty() && sys.sys2 != sys.sys1) {
      left_col->addWidget(new QLabel(
          z7::ui::archive_support::from_native_string(sys.sys2).trimmed(), this));
    }

    auto* button_row = new QHBoxLayout();
    auto* help_button = new QPushButton(L(409), this);
    auto* cancel_button = new QPushButton(L(402), this);
#ifdef Z7_TESTING
    help_button->setObjectName(QStringLiteral("benchmarkHelpButton"));
    cancel_button->setObjectName(QStringLiteral("benchmarkCancelButton"));
#endif
    help_button->setEnabled(false);
    help_button->setMinimumWidth(110);
    cancel_button->setMinimumWidth(110);
    help_button->setMinimumHeight(34);
    cancel_button->setMinimumHeight(34);
    button_row->addWidget(help_button);
    button_row->addStretch();
    button_row->addWidget(cancel_button);
    connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
    left_col->addLayout(button_row);

    connect(restart_button_, &QPushButton::clicked, this, &BenchmarkDialog::on_restart_clicked);
    connect(stop_button_, &QPushButton::clicked, this, &BenchmarkDialog::on_stop_clicked);

    connect(dictionary_combo_,
            &QComboBox::currentTextChanged,
            this,
            &BenchmarkDialog::on_selector_changed);
    connect(threads_combo_,
            &QComboBox::currentTextChanged,
            this,
            &BenchmarkDialog::on_selector_changed);
    connect(iterations_combo_,
            &QComboBox::currentTextChanged,
            this,
            &BenchmarkDialog::on_selector_changed);

    refresh_memory_estimate();
  }

  elapsed_timer_ = new QTimer(this);
  elapsed_timer_->setInterval(300);
  connect(elapsed_timer_, &QTimer::timeout, this, &BenchmarkDialog::update_elapsed_label);

  reset_result_view();
  reset_log_view();
  update_passed_label();
  set_running(false);
}

}  // namespace z7::ui::gui
