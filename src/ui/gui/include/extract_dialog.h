#pragma once

#include <QDialog>

#include "dialog_command_options.h"

class QComboBox;
class QCheckBox;
class QGroupBox;
class QDialogButtonBox;
class QLineEdit;
class QPushButton;

namespace z7::ui::gui {

class ExtractDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ExtractDialog(const ExtractCommandOptions& initial,
                         QWidget* parent = nullptr);

  ExtractCommandOptions options() const;

 private:
  void load_settings();
  void save_settings(const ExtractCommandOptions& options) const;
  void update_password_echo_mode();
  static QString lang_or(uint32_t id);

  QComboBox* output_dir_combo_ = nullptr;
  QPushButton* browse_button_ = nullptr;
  QCheckBox* split_dest_checkbox_ = nullptr;
  QLineEdit* split_dest_edit_ = nullptr;
  QComboBox* path_mode_combo_ = nullptr;
  QComboBox* overwrite_combo_ = nullptr;
  QCheckBox* eliminate_dup_checkbox_ = nullptr;
  QGroupBox* password_group_ = nullptr;
  QLineEdit* password_edit_ = nullptr;
  QCheckBox* show_password_checkbox_ = nullptr;
  QCheckBox* restore_security_checkbox_ = nullptr;
  QDialogButtonBox* buttons_ = nullptr;
};

}  // namespace z7::ui::gui
