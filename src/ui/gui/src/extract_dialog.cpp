#include "extract_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "app_startup_qt.h"
#include "portable_settings.h"
#include "archive_string_codec_qt.h"
#include "official_lang_catalog.h"
#include "path_history_utils.h"
#include "platform_support.h"
#include "shell_integration_menu.h"

namespace z7::ui::gui {
namespace {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

constexpr int kExtractHistoryMax = 16;
constexpr auto kSettingsExtractPathHistory = "Extraction/PathHistory";
constexpr auto kSettingsExtractSplitDest = "Extraction/SplitDest";
constexpr auto kSettingsExtractMode = "Extraction/ExtractMode";
constexpr auto kSettingsExtractOverwriteMode = "Extraction/OverwriteMode";
constexpr auto kSettingsExtractElimDup = "Extraction/ElimDup";
constexpr auto kSettingsExtractShowPassword = "Extraction/ShowPassword";
constexpr auto kSettingsExtractSecurity = "Extraction/Security";

constexpr int kOriginalPathModeFullPaths = 0;
constexpr int kOriginalPathModeNoPaths = 2;
constexpr int kOriginalPathModeAbsPaths = 3;
constexpr int kOriginalOverwriteAsk = 0;
constexpr int kOriginalOverwriteOverwrite = 1;
constexpr int kOriginalOverwriteSkip = 2;
constexpr int kOriginalOverwriteRename = 3;
constexpr int kOriginalOverwriteRenameExisting = 4;

QString to_lower_ascii(QString value) {
  for (int i = 0; i < value.size(); ++i) {
    value[i] = value[i].toLower();
  }
  return value;
}

QString normalized_path(QString value) {
  value = QDir::fromNativeSeparators(value.trimmed());
  return value;
}

QStringList normalized_history(QStringList history,
                               const QString& new_item,
                               int max_items = kExtractHistoryMax) {
  return z7::ui::common::normalized_path_history(history,
                                                 new_item,
                                                 max_items);
}

int find_combo_data(const QComboBox* combo, const QString& data) {
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toString() == data) {
      return i;
    }
  }
  return -1;
}

bool restore_security_supported() {
  return z7::ui::runtime_support::is_platform_supported(
      z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
}

QString path_mode_data_from_original_value(int value) {
  switch (value) {
    case kOriginalPathModeNoPaths:
      return QStringLiteral("no");
    case kOriginalPathModeAbsPaths:
      return QStringLiteral("absolute");
    case kOriginalPathModeFullPaths:
    default:
      return QStringLiteral("full");
  }
}

int original_value_from_path_mode_data(const QString& value) {
  const QString normalized = to_lower_ascii(value.trimmed());
  if (normalized == QStringLiteral("no")) {
    return kOriginalPathModeNoPaths;
  }
  if (normalized == QStringLiteral("absolute")) {
    return kOriginalPathModeAbsPaths;
  }
  return kOriginalPathModeFullPaths;
}

QString overwrite_data_from_original_value(int value) {
  switch (value) {
    case kOriginalOverwriteOverwrite:
      return QStringLiteral("-aoa");
    case kOriginalOverwriteSkip:
      return QStringLiteral("-aos");
    case kOriginalOverwriteRename:
      return QStringLiteral("-aot");
    case kOriginalOverwriteRenameExisting:
      return QStringLiteral("-aou");
    case kOriginalOverwriteAsk:
    default:
      return QString();
  }
}

int original_value_from_overwrite_data(const QString& value) {
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("-aoa")) {
    return kOriginalOverwriteOverwrite;
  }
  if (normalized == QStringLiteral("-aos")) {
    return kOriginalOverwriteSkip;
  }
  if (normalized == QStringLiteral("-aot")) {
    return kOriginalOverwriteRename;
  }
  if (normalized == QStringLiteral("-aou")) {
    return kOriginalOverwriteRenameExisting;
  }
  return kOriginalOverwriteAsk;
}

QString planned_extract_history_path(const ExtractCommandOptions& options) {
  const QString output_dir = normalized_path(
      z7::ui::archive_support::from_native_string(options.output_dir));
  if (!options.split_dest_enabled) {
    return output_dir;
  }
  const QString split_name = normalized_path(
      z7::ui::archive_support::from_native_string(options.split_dest_name));
  if (split_name.isEmpty()) {
    return output_dir;
  }
  return QDir(output_dir).filePath(split_name);
}

}  // namespace

QString ExtractDialog::lang_or(uint32_t id) {
  return strip_mnemonic(L(id));
}

ExtractDialog::ExtractDialog(const ExtractCommandOptions& initial,
                             QWidget* parent)
    : QDialog(parent) {
  QString title = lang_or(3400);
  const QString archive_path = z7::ui::archive_support::from_native_string(initial.archive_name).trimmed();
  const QString archive_file_name = QFileInfo(archive_path).fileName();
  if (!archive_path.isEmpty()) {
    title += QStringLiteral(" : ") + QDir::toNativeSeparators(archive_path);
  }
  setWindowTitle(title);
  resize(920, 520);
  setMinimumSize(840, 440);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 10, 12, 12);
  root->setSpacing(8);

  auto* extract_to_label = new QLabel(lang_or(3401), this);
  root->addWidget(extract_to_label);

  auto* path_row = new QWidget(this);
  auto* path_layout = new QHBoxLayout(path_row);
  path_layout->setContentsMargins(0, 0, 0, 0);
  output_dir_combo_ = new QComboBox(path_row);
#ifdef Z7_TESTING
  output_dir_combo_->setObjectName(QStringLiteral("extractOutputCombo"));
#endif
  output_dir_combo_->setEditable(true);
  browse_button_ = new QPushButton(QStringLiteral("..."), path_row);
#ifdef Z7_TESTING
  browse_button_->setObjectName(QStringLiteral("extractBrowseButton"));
#endif
  browse_button_->setMinimumWidth(52);
  path_layout->addWidget(output_dir_combo_, 1);
  path_layout->addWidget(browse_button_);
  root->addWidget(path_row);

  auto* body_row = new QHBoxLayout();
  body_row->setSpacing(14);

  auto* left_column = new QVBoxLayout();
  left_column->setSpacing(8);

  auto* split_row = new QWidget(this);
  auto* split_layout = new QHBoxLayout(split_row);
  split_layout->setContentsMargins(0, 0, 0, 0);
  split_dest_checkbox_ = new QCheckBox(split_row);
#ifdef Z7_TESTING
  split_dest_checkbox_->setObjectName(QStringLiteral("extractSplitDestCheckBox"));
#endif
  split_dest_edit_ = new QLineEdit(split_row);
#ifdef Z7_TESTING
  split_dest_edit_->setObjectName(QStringLiteral("extractSplitDestEdit"));
#endif
  split_layout->addWidget(split_dest_checkbox_);
  split_layout->addWidget(split_dest_edit_, 1);
  left_column->addWidget(split_row);
  left_column->addSpacing(8);

  auto* path_mode_label = new QLabel(lang_or(3410), this);
  left_column->addWidget(path_mode_label);
  path_mode_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
  path_mode_combo_->setObjectName(QStringLiteral("extractPathModeCombo"));
#endif
  path_mode_combo_->addItem(lang_or(3411), QStringLiteral("full"));
  path_mode_combo_->addItem(lang_or(3412), QStringLiteral("no"));
  path_mode_combo_->addItem(lang_or(3413), QStringLiteral("absolute"));
  left_column->addWidget(path_mode_combo_);

  eliminate_dup_checkbox_ = new QCheckBox(
      lang_or(3430),
      this);
#ifdef Z7_TESTING
  eliminate_dup_checkbox_->setObjectName(QStringLiteral("extractEliminateDupCheckBox"));
#endif
  left_column->addWidget(eliminate_dup_checkbox_);
  left_column->addSpacing(6);

  auto* overwrite_label = new QLabel(lang_or(3420), this);
  left_column->addWidget(overwrite_label);
  overwrite_combo_ = new QComboBox(this);
#ifdef Z7_TESTING
  overwrite_combo_->setObjectName(QStringLiteral("extractOverwriteCombo"));
#endif
  overwrite_combo_->addItem(lang_or(3421), QStringLiteral(""));
  overwrite_combo_->addItem(lang_or(3422),
                            QStringLiteral("-aoa"));
  overwrite_combo_->addItem(lang_or(3423),
                            QStringLiteral("-aos"));
  overwrite_combo_->addItem(lang_or(3424),
                            QStringLiteral("-aot"));
  overwrite_combo_->addItem(lang_or(3425),
                            QStringLiteral("-aou"));
  left_column->addWidget(overwrite_combo_);
  left_column->addStretch(1);

  auto* right_column = new QVBoxLayout();
  right_column->setSpacing(8);

  password_group_ = new QGroupBox(lang_or(3807), this);
  auto* password_layout = new QVBoxLayout(password_group_);
  password_layout->setContentsMargins(10, 16, 10, 10);
  password_layout->setSpacing(8);
  password_edit_ = new QLineEdit(password_group_);
#ifdef Z7_TESTING
  password_edit_->setObjectName(QStringLiteral("extractPasswordEdit"));
#endif
  password_edit_->setEchoMode(QLineEdit::Password);
  show_password_checkbox_ =
      new QCheckBox(lang_or(3803), password_group_);
#ifdef Z7_TESTING
  show_password_checkbox_->setObjectName(QStringLiteral("extractShowPasswordCheckBox"));
#endif
  password_layout->addWidget(password_edit_);
  password_layout->addWidget(show_password_checkbox_);
  password_layout->addStretch(1);
  right_column->addWidget(password_group_);

  restore_security_checkbox_ =
      new QCheckBox(lang_or(3431), this);
#ifdef Z7_TESTING
  restore_security_checkbox_->setObjectName(QStringLiteral("extractRestoreSecurityCheckBox"));
#endif
  const z7::ui::runtime_support::PlatformRestrictionUi restore_security_ui =
      z7::ui::runtime_support::platform_restriction_ui(
          restore_security_checkbox_->text(),
          z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
  restore_security_checkbox_->setText(restore_security_ui.text);
  restore_security_checkbox_->setToolTip(restore_security_ui.tooltip);
  right_column->addWidget(restore_security_checkbox_);
  right_column->addStretch(1);

  body_row->addLayout(left_column, 3);
  body_row->addLayout(right_column, 2);
  root->addLayout(body_row, 1);

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok |
                                      QDialogButtonBox::Cancel |
                                      QDialogButtonBox::Help,
                                  Qt::Horizontal,
                                  this);
#ifdef Z7_TESTING
  buttons_->setObjectName(QStringLiteral("extractDialogButtons"));
#endif
  z7::platform::qt::apply_dialog_button_baseline(buttons_);
  if (QPushButton* ok_button = buttons_->button(QDialogButtonBox::Ok)) {
    ok_button->setText(L(401));
  }
  if (QPushButton* cancel_button = buttons_->button(QDialogButtonBox::Cancel)) {
    cancel_button->setText(L(402));
  }
  if (QPushButton* help_button = buttons_->button(QDialogButtonBox::Help)) {
    help_button->setText(lang_or(409));
    help_button->setEnabled(false);
  }
  root->addWidget(buttons_);

  connect(split_dest_checkbox_, &QCheckBox::toggled, split_dest_edit_, &QWidget::setVisible);
  connect(show_password_checkbox_, &QCheckBox::toggled, this, [this](bool) {
    update_password_echo_mode();
  });
  connect(path_mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    const bool no_paths = path_mode_combo_->currentData().toString() == QStringLiteral("no");
    eliminate_dup_checkbox_->setEnabled(!no_paths);
    if (no_paths) {
      eliminate_dup_checkbox_->setChecked(false);
    }
  });
  connect(browse_button_, &QPushButton::clicked, this, [this]() {
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        lang_or(3402),
        output_dir_combo_->currentText());
    if (!selected.isEmpty()) {
      output_dir_combo_->setEditText(QDir::toNativeSeparators(selected));
    }
  });
  connect(buttons_, &QDialogButtonBox::helpRequested, this, [this]() {
    QMessageBox::information(this,
                             lang_or(3400),
                             lang_or(3402));
  });
  connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
    const ExtractCommandOptions current = options();
    save_settings(current);
    QDialog::accept();
  });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  load_settings();

  QString output_dir = normalized_path(z7::ui::archive_support::from_native_string(initial.output_dir));
  if (output_dir.isEmpty()) {
    output_dir = normalized_path(QDir::currentPath());
  }

  QString split_name = z7::ui::archive_support::from_native_string(initial.split_dest_name).trimmed();
  bool split_enabled = initial.split_dest_enabled;
  if (split_name.isEmpty() && !archive_file_name.isEmpty()) {
    const QString expected =
        z7::shell_integration::shell_integration_extract_subfolder_name(
            archive_file_name);
    QString normalized_output = output_dir;
    while (normalized_output.endsWith(QLatin1Char('/'))) {
      normalized_output.chop(1);
    }
    const QFileInfo out_info(normalized_output);
    if (out_info.fileName().compare(expected, Qt::CaseInsensitive) == 0) {
      split_enabled = true;
      split_name = expected;
      output_dir = out_info.dir().absolutePath();
    }
  }

  output_dir_combo_->setEditText(QDir::toNativeSeparators(output_dir));
  split_dest_checkbox_->setChecked(split_enabled);
  split_dest_edit_->setText(split_name);
  split_dest_edit_->setVisible(split_enabled);

  if (!initial.overwrite_switch.empty()) {
    const int index = find_combo_data(overwrite_combo_, z7::ui::archive_support::from_native_string(initial.overwrite_switch));
    if (index >= 0) {
      overwrite_combo_->setCurrentIndex(index);
    }
  }

  if (!initial.path_mode.empty()) {
    const int index = find_combo_data(path_mode_combo_, z7::ui::archive_support::from_native_string(initial.path_mode));
    if (index >= 0) {
      path_mode_combo_->setCurrentIndex(index);
    }
  }

  if (initial.eliminate_root_duplication) {
    eliminate_dup_checkbox_->setChecked(true);
  }
  if (!initial.password.empty()) {
    password_edit_->setText(z7::ui::archive_support::from_native_string(initial.password));
  }
  if (initial.show_password) {
    show_password_checkbox_->setChecked(true);
  }
  if (initial.restore_file_security) {
    restore_security_checkbox_->setChecked(true);
  }

  if (!restore_security_supported()) {
    restore_security_checkbox_->setChecked(false);
    restore_security_checkbox_->setEnabled(false);
  }

  update_password_echo_mode();
}

ExtractCommandOptions ExtractDialog::options() const {
  ExtractCommandOptions out;
  out.output_dir = z7::ui::archive_support::to_native_string(normalized_path(output_dir_combo_->currentText()));
  out.overwrite_switch = z7::ui::archive_support::to_native_string(overwrite_combo_->currentData().toString());
  out.path_mode = z7::ui::archive_support::to_native_string(path_mode_combo_->currentData().toString());
  out.eliminate_root_duplication = eliminate_dup_checkbox_->isChecked();
  out.password = z7::ui::archive_support::to_native_string(password_edit_->text());
  out.show_password = show_password_checkbox_->isChecked();
  out.restore_file_security =
      restore_security_supported() && restore_security_checkbox_->isChecked();
  out.split_dest_enabled = split_dest_checkbox_->isChecked();
  out.split_dest_name = z7::ui::archive_support::to_native_string(split_dest_edit_->text().trimmed());
  return out;
}

void ExtractDialog::load_settings() {
  z7::platform::qt::PortableSettings settings;
  const QStringList history = normalized_history(
      settings.value(QString::fromLatin1(kSettingsExtractPathHistory)).toStringList(),
      QString());
  for (const QString& entry : history) {
    const QString normalized = z7::ui::common::normalize_path_history_entry(entry);
    if (!normalized.isEmpty()) {
      output_dir_combo_->addItem(QDir::toNativeSeparators(normalized));
    }
  }

  split_dest_checkbox_->setChecked(
      settings.value(QString::fromLatin1(kSettingsExtractSplitDest), true).toBool());

  const QString path_mode =
      path_mode_data_from_original_value(
          settings.value(QString::fromLatin1(kSettingsExtractMode),
                         kOriginalPathModeFullPaths)
              .toInt());
  const int path_mode_index = find_combo_data(path_mode_combo_, path_mode);
  if (path_mode_index >= 0) {
    path_mode_combo_->setCurrentIndex(path_mode_index);
  }

  const QString overwrite =
      overwrite_data_from_original_value(
          settings.value(QString::fromLatin1(kSettingsExtractOverwriteMode),
                         kOriginalOverwriteAsk)
              .toInt());
  const int overwrite_index = find_combo_data(overwrite_combo_, overwrite);
  if (overwrite_index >= 0) {
    overwrite_combo_->setCurrentIndex(overwrite_index);
  }

  eliminate_dup_checkbox_->setChecked(
      settings.value(QString::fromLatin1(kSettingsExtractElimDup), true).toBool());
  show_password_checkbox_->setChecked(
      settings.value(QString::fromLatin1(kSettingsExtractShowPassword), false).toBool());
  restore_security_checkbox_->setChecked(
      restore_security_supported() &&
      settings.value(QString::fromLatin1(kSettingsExtractSecurity), false).toBool());
}

void ExtractDialog::save_settings(const ExtractCommandOptions& options) const {
  z7::platform::qt::PortableSettings settings;
  const QStringList history =
      settings.value(QString::fromLatin1(kSettingsExtractPathHistory)).toStringList();
  settings.setValue(QString::fromLatin1(kSettingsExtractPathHistory),
                    normalized_history(history,
                                       planned_extract_history_path(options)));
  settings.setValue(QString::fromLatin1(kSettingsExtractSplitDest), options.split_dest_enabled);
  settings.setValue(QString::fromLatin1(kSettingsExtractMode),
                    original_value_from_path_mode_data(
                        QString::fromStdString(options.path_mode)));
  settings.setValue(QString::fromLatin1(kSettingsExtractOverwriteMode),
                    original_value_from_overwrite_data(
                        QString::fromStdString(options.overwrite_switch)));
  settings.setValue(QString::fromLatin1(kSettingsExtractElimDup),
                    options.eliminate_root_duplication);
  settings.setValue(QString::fromLatin1(kSettingsExtractShowPassword), options.show_password);
  settings.setValue(QString::fromLatin1(kSettingsExtractSecurity),
                    options.restore_file_security);
  settings.sync();
}

void ExtractDialog::update_password_echo_mode() {
  const bool show = show_password_checkbox_->isChecked();
  password_edit_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
}

}  // namespace z7::ui::gui
