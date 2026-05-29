#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;
class QWidget;
class QCloseEvent;

namespace z7::ui::runtime_support {

struct TaskProgressDialogBehavior {
  int initial_width = 920;
  int initial_height = 560;
  bool modal = true;
  bool delete_on_close = false;
  bool running_close_requests_cancel = false;
  bool confirm_cancel_only_for_test_mode = false;
  bool running_stage_uses_test_caption = false;
  bool normalize_metric_label_colons = false;
  bool append_blank_log_lines = false;
  bool parse_extended_progress_log = false;
  bool freeze_title_after_result_mode = false;
  const char* dialog_object_name = nullptr;
  const char* result_messages_view_object_name = nullptr;
  const char* background_button_object_name = nullptr;
  const char* pause_button_object_name = nullptr;
  const char* cancel_button_object_name = nullptr;
  const char* close_button_object_name = nullptr;
};

struct TaskProgressRatioInfo {
  bool input_size_known = false;
  quint64 input_size = 0;
  bool output_size_known = false;
  quint64 output_size = 0;
  bool compressing_mode = true;
};

class TaskProgressDialogBase : public QDialog {
  Q_OBJECT

 public:
  void set_header(const QString& text);
  void set_stage(const QString& text);
  void append_log(const QString& line);
  void set_percent(int value);
  void set_detailed_progress(bool totals_known,
                             quint64 total_bytes,
                             quint64 completed_bytes,
                             quint64 total_files,
                             quint64 completed_files,
                             quint64 error_count,
                             const std::optional<TaskProgressRatioInfo>& ratio_info,
                             const QString& current_path);
  void set_pause_available(bool available);
  void set_running(bool running);
  void set_test_mode(bool enabled);
  void append_failure_result_message(const QString& message);
  void set_failure_result_messages(const QStringList& messages);
  void set_failure_result_mode();
  void set_cancel_confirmation_handler(const std::function<int()>& handler);

 signals:
  void background_requested(bool backgrounded);
  void pause_requested();
  void resume_requested();
  void cancel_requested();

 protected:
  explicit TaskProgressDialogBase(const TaskProgressDialogBehavior& behavior,
                                  QWidget* parent = nullptr);
  void closeEvent(QCloseEvent* event) override;
  void reject() override;
  void set_result_mode_impl();

 private:
  QString label_text_with_optional_colon(const QString& text) const;
  void set_paused(bool paused);
  void set_backgrounded(bool backgrounded);
  void refresh_metrics();
  void update_title();
  void update_current_path_labels();
  void parse_progress_log_line(const QString& line);
  QString display_stage_text() const;
  void on_cancel_clicked();

  TaskProgressDialogBehavior behavior_;

  QWidget* metrics_widget_ = nullptr;
  QLabel* elapsed_value_ = nullptr;
  QLabel* remaining_value_ = nullptr;
  QLabel* files_value_ = nullptr;
  QLabel* files_total_value_ = nullptr;
  QLabel* errors_label_ = nullptr;
  QLabel* errors_value_ = nullptr;
  QLabel* total_size_value_ = nullptr;
  QLabel* speed_value_ = nullptr;
  QLabel* processed_value_ = nullptr;
  QLabel* packed_value_ = nullptr;
  QLabel* ratio_value_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* current_path_label_ = nullptr;
  QLabel* current_file_label_ = nullptr;
  QProgressBar* progress_bar_ = nullptr;
  QPlainTextEdit* log_view_ = nullptr;
  QTableWidget* result_messages_view_ = nullptr;
  QPushButton* background_button_ = nullptr;
  QPushButton* pause_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QTimer* refresh_timer_ = nullptr;

  QString header_text_;
  QString operation_text_;
  QString target_text_;
  QString stage_text_;
  QString current_path_;

  int percent_ = -1;
  bool running_ = false;
  bool paused_ = false;
  bool backgrounded_ = false;
  bool totals_known_ = false;
  bool pause_available_ = true;
  bool test_mode_ = false;
  int failure_result_message_count_ = 0;
  std::function<int()> cancel_confirmation_handler_;

  quint64 total_bytes_ = 0;
  quint64 completed_bytes_ = 0;
  quint64 total_files_ = 0;
  quint64 completed_files_ = 0;
  quint64 error_count_ = 0;
  std::optional<TaskProgressRatioInfo> ratio_info_;

  qint64 elapsed_ms_ = 0;
  qint64 started_ms_ = -1;
  qint64 paused_started_ms_ = -1;
  qint64 paused_total_ms_ = 0;
};

}  // namespace z7::ui::runtime_support
