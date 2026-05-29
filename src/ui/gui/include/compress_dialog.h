#pragma once

#include <functional>

#include <QDialog>

#include "dialog_command_options.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace z7::ui::gui {

class CompressDialog : public QDialog {
  Q_OBJECT

 public:
  explicit CompressDialog(const CompressCommandOptions& initial,
                          QWidget* parent = nullptr);

  CompressCommandOptions options() const;

 protected:
  void accept() override;

 private:
  void build_ui();
  void populate_from_initial(const CompressCommandOptions& initial);
  void on_options_clicked();
  void on_help_clicked();
  void populate_format_combo();
  void recompute_state(bool keep_current_method,
                       bool keep_current_dictionary,
                       bool keep_current_word_size,
                       bool keep_current_solid,
                       bool keep_current_threads);
  void rebuild_level_combo();
  void rebuild_method_combo(bool keep_current_method);
  void rebuild_dictionary_combo(bool keep_current_dictionary);
  void rebuild_word_size_combo(bool keep_current_word_size);
  void rebuild_solid_combo(bool keep_current_solid);
  void rebuild_threads_combo(bool keep_current_threads);
  void update_encryption_controls();
  void update_sfx_controls();
  void update_memory_visibility();
  void update_memory_labels();
  void update_password_echo_mode();
  bool is_updating_controls() const;
  void set_archive_fields_from_path(const QString& full_path);
  void replace_archive_name_extension_for_current_format();
  QString archive_name_validation_error() const;
  void load_archive_path_history();
  QString initial_or_saved_archive_type(
      const CompressCommandOptions& initial) const;
  bool saved_show_password() const;
  bool initial_or_saved_encrypt_headers(
      const CompressCommandOptions& initial) const;
  void apply_persistent_format_options(
      const QString& format_id,
      const CompressCommandOptions* explicit_options);
  void save_current_format_settings() const;
  void save_format_settings(const QString& format_id) const;
  void save_persistent_settings() const;
  QString compose_archive_path() const;
  QString current_format_id() const;
  QString default_encryption_method_for_current_format() const;
  QString selected_encryption_method_spec() const;
  QString current_output_suffix() const;
  bool current_format_keeps_original_name() const;
  bool is_sfx_enabled() const;
  static void set_combo_enabled_if_multiple(QComboBox* combo);

  static QString lang_or(uint32_t id);

  // Left panel
  QString archive_dir_prefix_;
  QLabel* archive_dir_prefix_label_ = nullptr;
  QComboBox* archive_name_combo_ = nullptr;
  QComboBox* format_combo_ = nullptr;
  QComboBox* level_combo_ = nullptr;
  QComboBox* method_combo_ = nullptr;
  QComboBox* dictionary_combo_ = nullptr;
  QComboBox* word_size_combo_ = nullptr;
  QComboBox* solid_combo_ = nullptr;
  QComboBox* threads_combo_ = nullptr;
  QLabel* hardware_threads_label_ = nullptr;
  QLabel* compress_memory_title_label_ = nullptr;
  QLabel* compress_memory_label_ = nullptr;
  QLabel* decompress_memory_title_label_ = nullptr;
  QLabel* decompress_memory_label_ = nullptr;
  QComboBox* volume_combo_ = nullptr;
  QLineEdit* parameters_edit_ = nullptr;
  QPushButton* options_button_ = nullptr;

  // Right panel
  QComboBox* update_mode_combo_ = nullptr;
  QComboBox* path_mode_combo_ = nullptr;
  QCheckBox* create_sfx_checkbox_ = nullptr;
  QCheckBox* compress_shared_checkbox_ = nullptr;
  QCheckBox* delete_after_checkbox_ = nullptr;

  QLabel* password_label_ = nullptr;
  QLineEdit* password_edit_ = nullptr;
  QLabel* reenter_password_label_ = nullptr;
  QLineEdit* reenter_password_edit_ = nullptr;
  QCheckBox* show_password_checkbox_ = nullptr;
  QLabel* encryption_method_label_ = nullptr;
  QComboBox* encryption_method_combo_ = nullptr;
  QCheckBox* encrypt_headers_checkbox_ = nullptr;

  QLabel* error_label_ = nullptr;
  QPushButton* ok_button_ = nullptr;
  bool updating_controls_ = false;
  bool keep_archive_name_extension_ = false;
  bool single_file_input_ = false;
  QString single_file_name_;
  QString generated_archive_extension_;
  QString active_format_settings_id_;
  AddTaskOpaqueState opaque_add_task_;
};

}  // namespace z7::ui::gui
