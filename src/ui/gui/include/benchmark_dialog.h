#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

#include <memory>

#include "archive_session.h"
#include "dialog_command_options.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QShowEvent;

namespace z7::ui::gui {

class BenchmarkDialog : public QDialog {
 Q_OBJECT

 public:
  explicit BenchmarkDialog(const BenchmarkCommandOptions& initial,
                           QWidget* parent = nullptr);

  BenchmarkCommandOptions options() const;
  int last_exit_code() const;
  void dispatch_operation_event(const z7::app::OperationEvent& event);

 protected:
  void showEvent(QShowEvent* event) override;
  void reject() override;

 private:
  void start_benchmark();
  void stop_benchmark();
  void start_benchmark_pass(const z7::app::BenchmarkRequest& request);
  void on_benchmark_pass_finished(const z7::app::OperationOutcome& outcome);
  void finalize_benchmark_run(const z7::app::OperationOutcome& outcome);
  void reset_result_view();
  void reset_log_view();
  void set_running(bool running);
  void append_log_line(const QString& line);
  void handle_operation_event(const z7::app::OperationEvent& event);
  void apply_typed_snapshot(const z7::app::BenchmarkTypedSnapshot& snapshot);
  void apply_typed_summary(const z7::app::BenchmarkTypedSummary& summary);
  void update_elapsed_label();
  void update_passed_label();
  void refresh_memory_estimate();
  void on_selector_changed();
  uint32_t selected_iterations() const;
  void append_pass_row();
  void rebuild_log_view();

  void on_restart_clicked();
  void on_stop_clicked();

  QComboBox* iterations_combo_ = nullptr;
  QComboBox* threads_combo_ = nullptr;
  QComboBox* dictionary_combo_ = nullptr;
  QLabel* hardware_threads_label_ = nullptr;
  QLabel* memory_label_ = nullptr;
  QLabel* elapsed_label_ = nullptr;
  QLabel* passed_label_ = nullptr;
  QLabel* error_label_ = nullptr;
  QPlainTextEdit* freq_log_edit_ = nullptr;
  QLabel* freq_log_label_ = nullptr;

  QLabel* compress_current_size_ = nullptr;
  QLabel* compress_current_speed_ = nullptr;
  QLabel* compress_current_usage_ = nullptr;
  QLabel* compress_current_rpu_ = nullptr;
  QLabel* compress_current_rating_ = nullptr;

  QLabel* compress_result_size_ = nullptr;
  QLabel* compress_result_speed_ = nullptr;
  QLabel* compress_result_usage_ = nullptr;
  QLabel* compress_result_rpu_ = nullptr;
  QLabel* compress_result_rating_ = nullptr;

  QLabel* decompress_current_size_ = nullptr;
  QLabel* decompress_current_speed_ = nullptr;
  QLabel* decompress_current_usage_ = nullptr;
  QLabel* decompress_current_rpu_ = nullptr;
  QLabel* decompress_current_rating_ = nullptr;

  QLabel* decompress_result_size_ = nullptr;
  QLabel* decompress_result_speed_ = nullptr;
  QLabel* decompress_result_usage_ = nullptr;
  QLabel* decompress_result_rpu_ = nullptr;
  QLabel* decompress_result_rating_ = nullptr;

  QLabel* total_usage_label_ = nullptr;
  QLabel* total_rpu_label_ = nullptr;
  QLabel* total_rating_label_ = nullptr;

  QPushButton* restart_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;

  z7::app::ArchiveEngine engine_;
  z7::app::ArchiveSession active_task_;
  std::shared_ptr<z7::app::IArchiveDelegate> active_delegate_;
  z7::app::BenchmarkRequest pending_benchmark_request_;
  uint32_t pending_passes_ = 0;
  QTimer* elapsed_timer_ = nullptr;
  QElapsedTimer elapsed_clock_;

  bool first_show_started_ = false;
  bool restart_requested_ = false;
  bool close_after_stop_ = false;
  bool benchmark_running_ = false;
  bool suppress_auto_restart_ = false;
  bool total_mode_ui_ = false;
  int passed_count_ = 0;
  int prev_dict_log_ = -1;
  int last_exit_code_ = 0;
  BenchmarkCommandOptions initial_options_;

  QStringList freq_lines_;
  QStringList pass_rows_;
  QString current_pass_compr_rating_;
  QString current_pass_decompr_rating_;
  QString current_pass_total_rating_;
  QString current_pass_cpu_usage_;
  bool current_pass_has_summary_ = false;
  bool benchmark_finished_ = false;
  double sum_pass_compr_ = 0.0;
  double sum_pass_decompr_ = 0.0;
  double sum_pass_total_ = 0.0;
  double sum_pass_cpu_ = 0.0;
  int summed_pass_count_ = 0;
};

}  // namespace z7::ui::gui
