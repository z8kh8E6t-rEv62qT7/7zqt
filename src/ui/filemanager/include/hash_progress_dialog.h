#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace z7::ui::filemanager {

class HashProgressDialog : public QDialog {
  Q_OBJECT

 public:
  explicit HashProgressDialog(QWidget* parent = nullptr);

  void set_operation_name(const QString& method_name);
  void set_stage(const QString& text);
  void set_progress(bool totals_known,
                    quint64 total_bytes,
                    quint64 completed_bytes,
                    quint64 total_files,
                    quint64 completed_files,
                    quint64 error_count,
                    const QString& current_path);
  void set_running(bool running);
  bool is_paused() const;
  bool is_backgrounded() const;

 signals:
  void background_requested(bool backgrounded);
  void pause_requested();
  void resume_requested();
  void cancel_requested();

 private:
  void set_paused(bool paused);
  void set_backgrounded(bool backgrounded);
  void refresh_metrics();
  void update_title();
  void update_current_path_labels();

  QLabel* header_label_ = nullptr;
  QLabel* elapsed_value_ = nullptr;
  QLabel* remaining_value_ = nullptr;
  QLabel* files_value_ = nullptr;
  QLabel* errors_value_ = nullptr;
  QLabel* total_size_value_ = nullptr;
  QLabel* speed_value_ = nullptr;
  QLabel* processed_value_ = nullptr;
  QLabel* current_dir_value_ = nullptr;
  QLabel* current_file_value_ = nullptr;
  QProgressBar* progress_bar_ = nullptr;
  QPushButton* background_button_ = nullptr;
  QPushButton* pause_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QTimer* refresh_timer_ = nullptr;

  QString operation_name_;
  QString stage_text_;
  QString current_path_;
  bool running_ = false;
  bool paused_ = false;
  bool backgrounded_ = false;

  bool totals_known_ = false;
  quint64 total_bytes_ = 0;
  quint64 completed_bytes_ = 0;
  quint64 total_files_ = 0;
  quint64 completed_files_ = 0;
  quint64 error_count_ = 0;

  qint64 elapsed_ms_ = 0;
  qint64 started_ms_ = -1;
  qint64 paused_started_ms_ = -1;
  qint64 paused_total_ms_ = 0;
};

}  // namespace z7::ui::filemanager
