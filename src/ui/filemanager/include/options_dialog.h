#pragma once

#include <functional>

#include <QDialog>
#include <QString>
#include <QVector>
#include <Qt>

class QComboBox;
class QCheckBox;
class QCloseEvent;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QWidget;

namespace z7::ui::filemanager {

class OptionsDialog final : public QDialog {
 Q_OBJECT

 public:
  explicit OptionsDialog(QWidget* parent = nullptr);

  bool language_changed() const;

 protected:
  void closeEvent(QCloseEvent* event) override;

 signals:
  void settings_applied();

 private slots:
  void on_help_requested();
  void on_accept();
  void on_reject();
  void on_apply();
  void on_editor_command_changed();
  void on_language_selection_changed();
  void on_runtime_setting_control_changed();
  void on_large_pages_toggled(bool checked);
  void on_qt_setting_control_changed();

 private:
  void setup_ui();
  QWidget* create_editor_page();
  QWidget* create_folders_page();
  void load_editor_settings();
  bool apply_editor_settings();
  void browse_editor_path(QLineEdit* line_edit);
  void browse_folders_work_path();
  void build_settings_page();
  void build_qt_page();
  void populate_system_associations();
  void populate_context_menu_items();
  void populate_languages();
  void load_seven_zip_settings();
  bool apply_seven_zip_settings();
  void on_seven_zip_setting_control_changed();
  void on_folders_setting_control_changed();
  void load_runtime_settings();
  void save_runtime_settings() const;
  void update_memory_limit_controls_enabled();
  void load_qt_startup_settings();
  bool apply_qt_startup_settings();
  void update_language_info();
  void update_apply_button_state();
  void update_texts();
  void persist_exit_ui_state_once();
  void load_associations_table_column_widths();
  void save_associations_table_column_widths() const;

  QTabWidget* tabs_ = nullptr;
  QWidget* system_page_ = nullptr;
  QWidget* seven_zip_page_ = nullptr;
  QWidget* folders_page_ = nullptr;
  QWidget* editor_page_ = nullptr;
  QWidget* settings_page_ = nullptr;
  QWidget* qt_page_ = nullptr;
  QWidget* language_page_ = nullptr;
  QLabel* viewer_label_ = nullptr;
  QLabel* editor_label_ = nullptr;
  QLabel* diff_label_ = nullptr;
  QLineEdit* viewer_edit_ = nullptr;
  QLineEdit* editor_edit_ = nullptr;
  QLineEdit* diff_edit_ = nullptr;
  QPushButton* viewer_browse_button_ = nullptr;
  QPushButton* editor_browse_button_ = nullptr;
  QPushButton* diff_browse_button_ = nullptr;

  QTableWidget* associations_table_ = nullptr;
  QPushButton* current_user_add_button_ = nullptr;
  QPushButton* all_users_add_button_ = nullptr;
  QComboBox* zone_id_combo_ = nullptr;
  QListWidget* context_items_list_ = nullptr;
  QCheckBox* integrate_shell_checkbox_ = nullptr;
  QCheckBox* integrate_shell_32_checkbox_ = nullptr;
  QCheckBox* cascaded_menu_checkbox_ = nullptr;
  QCheckBox* menu_icons_checkbox_ = nullptr;
  QCheckBox* eliminate_dup_roots_checkbox_ = nullptr;
  QLabel* zone_id_label_ = nullptr;

  QLabel* language_label_ = nullptr;
  QComboBox* language_combo_ = nullptr;
  QPlainTextEdit* language_info_view_ = nullptr;

  QComboBox* qt_preferred_style_combo_ = nullptr;
  QComboBox* qt_hidpi_policy_combo_ = nullptr;
  QLabel* qt_restart_hint_label_ = nullptr;

  QCheckBox* show_dots_checkbox_ = nullptr;
  QCheckBox* show_real_icons_checkbox_ = nullptr;
  QCheckBox* full_row_checkbox_ = nullptr;
  QCheckBox* show_grid_checkbox_ = nullptr;
  QCheckBox* single_click_checkbox_ = nullptr;
  QCheckBox* alternative_selection_checkbox_ = nullptr;
  QRadioButton* folders_work_system_radio_ = nullptr;
  QRadioButton* folders_work_current_radio_ = nullptr;
  QRadioButton* folders_work_specified_radio_ = nullptr;
  QLineEdit* folders_work_path_edit_ = nullptr;
  QPushButton* folders_work_path_browse_button_ = nullptr;
  QCheckBox* folders_work_removable_only_checkbox_ = nullptr;
  QCheckBox* show_system_menu_checkbox_ = nullptr;
  QCheckBox* use_large_pages_checkbox_ = nullptr;
  QLabel* mem_usage_label_ = nullptr;
  QCheckBox* mem_limit_enable_checkbox_ = nullptr;
  QLabel* mem_colon_label_ = nullptr;
  QSpinBox* mem_limit_spin_ = nullptr;
  QLabel* mem_limit_suffix_label_ = nullptr;
  QLabel* folders_working_folder_label_ = nullptr;

  QDialogButtonBox* button_box_ = nullptr;
  QString initial_viewer_command_;
  QString initial_editor_command_;
  QString initial_diff_command_;
  bool editor_commands_dirty_ = false;
  bool language_changed_ = false;
  bool seven_zip_settings_dirty_ = false;
  bool folders_settings_dirty_ = false;
  bool runtime_settings_dirty_ = false;
  bool qt_settings_dirty_ = false;
  bool exit_ui_state_persisted_ = false;
  QString initial_language_id_;
  QString initial_qt_preferred_style_;
  bool initial_integrate_shell_ = true;
  bool initial_integrate_shell_32_ = true;
  bool initial_cascaded_menu_ = true;
  bool initial_menu_icons_ = false;
  bool initial_eliminate_dup_roots_ = true;
  int initial_zone_id_policy_index_ = 0;
  QVector<bool> initial_context_item_checks_;
  int initial_folders_work_mode_ = 0;
  QString initial_folders_work_path_;
  bool initial_folders_work_removable_only_ = true;
  Qt::HighDpiScaleFactorRoundingPolicy initial_qt_hidpi_policy_ =
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
};

}  // namespace z7::ui::filemanager
