// src/ui/filemanager/src/options/layout_folders.cpp
// Role: "Folders" options page UI and persistence.

#include "internal.h"

#include <QDir>

namespace z7::ui::filemanager {

using namespace options_internal;

namespace {

constexpr int kWorkModeSystem = 0;
constexpr int kWorkModeCurrent = 1;
constexpr int kWorkModeSpecified = 2;

}  // namespace

QWidget* OptionsDialog::create_folders_page() {
  auto* page = new QWidget(tabs_);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(24, 12, 24, 12);
  layout->setSpacing(10);

  auto* work_group = new QGroupBox(page);
#ifdef Z7_TESTING
  work_group->setObjectName(QStringLiteral("foldersWorkGroup"));
#endif
  auto* group_layout = new QVBoxLayout(work_group);
  group_layout->setContentsMargins(12, 12, 12, 12);
  group_layout->setSpacing(10);

  folders_working_folder_label_ = new QLabel(work_group);
#ifdef Z7_TESTING
  folders_working_folder_label_->setObjectName(QStringLiteral("foldersWorkingFolderLabel"));
#endif
  group_layout->addWidget(folders_working_folder_label_);

  folders_work_system_radio_ = new QRadioButton(work_group);
#ifdef Z7_TESTING
  folders_work_system_radio_->setObjectName(QStringLiteral("foldersWorkSystemRadio"));
#endif
  group_layout->addWidget(folders_work_system_radio_);

  folders_work_current_radio_ = new QRadioButton(work_group);
#ifdef Z7_TESTING
  folders_work_current_radio_->setObjectName(QStringLiteral("foldersWorkCurrentRadio"));
#endif
  group_layout->addWidget(folders_work_current_radio_);

  folders_work_specified_radio_ = new QRadioButton(work_group);
#ifdef Z7_TESTING
  folders_work_specified_radio_->setObjectName(QStringLiteral("foldersWorkSpecifiedRadio"));
#endif
  group_layout->addWidget(folders_work_specified_radio_);

  auto* path_row = new QHBoxLayout();
  path_row->setContentsMargins(20, 0, 0, 0);
  path_row->setSpacing(8);

  folders_work_path_edit_ = new QLineEdit(work_group);
#ifdef Z7_TESTING
  folders_work_path_edit_->setObjectName(QStringLiteral("foldersWorkPathEdit"));
#endif
  path_row->addWidget(folders_work_path_edit_, 1);

  folders_work_path_browse_button_ = new QPushButton(QStringLiteral("..."), work_group);
#ifdef Z7_TESTING
  folders_work_path_browse_button_->setObjectName(QStringLiteral("foldersWorkPathBrowseButton"));
#endif
  folders_work_path_browse_button_->setMinimumWidth(64);
  path_row->addWidget(folders_work_path_browse_button_);

  group_layout->addLayout(path_row);

  folders_work_removable_only_checkbox_ = new QCheckBox(work_group);
#ifdef Z7_TESTING
  folders_work_removable_only_checkbox_->setObjectName(
      QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
#endif
  group_layout->addWidget(folders_work_removable_only_checkbox_);

  layout->addWidget(work_group);
  layout->addStretch(1);

  auto update_path_controls = [this]() {
    const bool enable_path =
        folders_work_specified_radio_ != nullptr &&
        folders_work_specified_radio_->isChecked();
    if (folders_work_path_edit_ != nullptr) {
      folders_work_path_edit_->setEnabled(enable_path);
    }
    if (folders_work_path_browse_button_ != nullptr) {
      folders_work_path_browse_button_->setEnabled(enable_path);
    }
  };

  if (folders_work_system_radio_ != nullptr) {
    connect(folders_work_system_radio_, &QRadioButton::toggled, this, [this, update_path_controls](bool) {
      update_path_controls();
      on_folders_setting_control_changed();
    });
  }
  if (folders_work_current_radio_ != nullptr) {
    connect(folders_work_current_radio_, &QRadioButton::toggled, this, [this, update_path_controls](bool) {
      update_path_controls();
      on_folders_setting_control_changed();
    });
  }
  if (folders_work_specified_radio_ != nullptr) {
    connect(folders_work_specified_radio_,
            &QRadioButton::toggled,
            this,
            [this, update_path_controls](bool) {
              update_path_controls();
              on_folders_setting_control_changed();
            });
  }
  if (folders_work_path_edit_ != nullptr) {
    connect(folders_work_path_edit_,
            &QLineEdit::textChanged,
            this,
            [this](const QString&) { on_folders_setting_control_changed(); });
  }
  if (folders_work_path_browse_button_ != nullptr) {
    connect(folders_work_path_browse_button_, &QPushButton::clicked, this, [this]() {
      browse_folders_work_path();
    });
  }
  if (folders_work_removable_only_checkbox_ != nullptr) {
    connect(folders_work_removable_only_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_folders_setting_control_changed(); });
  }

  update_path_controls();
  return page;
}

void OptionsDialog::browse_folders_work_path() {
  if (folders_work_path_edit_ == nullptr) {
    return;
  }

  QString start_dir =
      QDir::fromNativeSeparators(folders_work_path_edit_->text().trimmed());
  if (start_dir.isEmpty()) {
    start_dir = QDir::currentPath();
  }
  const QString selected = QFileDialog::getExistingDirectory(
      this,
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2406)),
      start_dir,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (selected.isEmpty()) {
    return;
  }
  folders_work_path_edit_->setText(QDir::toNativeSeparators(selected));
}

void OptionsDialog::on_folders_setting_control_changed() {
  if (folders_work_system_radio_ == nullptr ||
      folders_work_current_radio_ == nullptr ||
      folders_work_specified_radio_ == nullptr ||
      folders_work_path_edit_ == nullptr ||
      folders_work_path_browse_button_ == nullptr ||
      folders_work_removable_only_checkbox_ == nullptr) {
    return;
  }

  const bool enable_path = folders_work_specified_radio_->isChecked();
  folders_work_path_edit_->setEnabled(enable_path);
  folders_work_path_browse_button_->setEnabled(enable_path);

  int current_mode = kWorkModeSystem;
  if (folders_work_current_radio_->isChecked()) {
    current_mode = kWorkModeCurrent;
  } else if (folders_work_specified_radio_->isChecked()) {
    current_mode = kWorkModeSpecified;
  }
  const QString current_path =
      QDir::fromNativeSeparators(folders_work_path_edit_->text().trimmed());
  const bool current_removable_only = folders_work_removable_only_checkbox_->isChecked();

  folders_settings_dirty_ =
      current_mode != initial_folders_work_mode_ ||
      current_path != initial_folders_work_path_ ||
      current_removable_only != initial_folders_work_removable_only_;
  update_apply_button_state();
}

}  // namespace z7::ui::filemanager
