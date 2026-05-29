// src/ui/filemanager/src/options/apply_pages_runtime.cpp
// Role: Runtime settings load/save logic for Options dialog pages.

#include "internal.h"

namespace z7::ui::filemanager {

using namespace options_internal;

namespace {

enum class WorkDirMode {
  kSystem = 0,
  kCurrent = 1,
  kSpecified = 2
};

WorkDirMode normalized_work_dir_mode(int raw_mode, const QString& path) {
  WorkDirMode mode = WorkDirMode::kSystem;
  switch (raw_mode) {
    case static_cast<int>(WorkDirMode::kSystem):
      mode = WorkDirMode::kSystem;
      break;
    case static_cast<int>(WorkDirMode::kCurrent):
      mode = WorkDirMode::kCurrent;
      break;
    case static_cast<int>(WorkDirMode::kSpecified):
      mode = WorkDirMode::kSpecified;
      break;
    default:
      mode = WorkDirMode::kSystem;
      break;
  }
  if (mode == WorkDirMode::kSpecified && path.trimmed().isEmpty()) {
    mode = WorkDirMode::kSystem;
  }
  return mode;
}

}  // namespace

void OptionsDialog::load_runtime_settings() {
  z7::platform::qt::PortableSettings settings;
  const DisplaySettings display_settings = load_display_settings(settings);
  const z7::ui::runtime_support::ExtractMemoryLimitSettings mem_settings =
      z7::ui::runtime_support::load_extract_memory_limit_settings();

  const QString work_path = settings
                                .value(QString::fromLatin1(kSettingsFmFoldersWorkDirPath), QString())
                                .toString();
  const WorkDirMode work_mode = normalized_work_dir_mode(
      settings.value(QString::fromLatin1(kSettingsFmFoldersWorkDirMode),
                     static_cast<int>(WorkDirMode::kSystem))
          .toInt(),
      work_path);
  const bool for_removable_only =
      settings.value(QString::fromLatin1(kSettingsFmFoldersWorkForRemovableOnly), true).toBool();
  const QString normalized_work_path = QDir::fromNativeSeparators(work_path.trimmed());

  const QSignalBlocker block_show_dots(show_dots_checkbox_);
  const QSignalBlocker block_show_real_icons(show_real_icons_checkbox_);
  const QSignalBlocker block_full_row(full_row_checkbox_);
  const QSignalBlocker block_show_grid(show_grid_checkbox_);
  const QSignalBlocker block_single_click(single_click_checkbox_);
  const QSignalBlocker block_alternative(alternative_selection_checkbox_);
  const QSignalBlocker block_large_pages(use_large_pages_checkbox_);
  const QSignalBlocker block_mem_enabled(mem_limit_enable_checkbox_);
  const QSignalBlocker block_mem_spin(mem_limit_spin_);
  const QSignalBlocker block_work_system(folders_work_system_radio_);
  const QSignalBlocker block_work_current(folders_work_current_radio_);
  const QSignalBlocker block_work_specified(folders_work_specified_radio_);
  const QSignalBlocker block_work_path(folders_work_path_edit_);
  const QSignalBlocker block_removable_only(folders_work_removable_only_checkbox_);

  if (show_dots_checkbox_ != nullptr) {
    show_dots_checkbox_->setChecked(display_settings.show_dots);
  }
  if (show_real_icons_checkbox_ != nullptr) {
    show_real_icons_checkbox_->setChecked(display_settings.show_real_file_icons);
  }
  if (full_row_checkbox_ != nullptr) {
    full_row_checkbox_->setChecked(display_settings.full_row_select);
  }
  if (show_grid_checkbox_ != nullptr) {
    show_grid_checkbox_->setChecked(display_settings.show_grid_lines);
  }
  if (single_click_checkbox_ != nullptr) {
    single_click_checkbox_->setChecked(display_settings.single_click_open);
  }
  if (alternative_selection_checkbox_ != nullptr) {
    alternative_selection_checkbox_->setChecked(display_settings.alternative_selection_mode);
  }
  if (use_large_pages_checkbox_ != nullptr) {
    use_large_pages_checkbox_->setEnabled(large_pages_supported());
    use_large_pages_checkbox_->setChecked(
        large_pages_supported() &&
        z7::ui::runtime_support::load_large_pages_enabled());
  }
  if (mem_limit_spin_ != nullptr) {
    const quint64 ram_bytes = detect_total_ram_bytes();
    mem_limit_spin_->setMaximum(max_mem_limit_gb(ram_bytes));
    mem_limit_spin_->setValue(mem_settings.limit_gb);
  }
  if (mem_limit_enable_checkbox_ != nullptr) {
    mem_limit_enable_checkbox_->setChecked(extract_memory_limit_supported() &&
                                           mem_settings.enabled);
  }
  update_memory_limit_controls_enabled();
  if (folders_work_system_radio_ != nullptr) {
    folders_work_system_radio_->setChecked(work_mode == WorkDirMode::kSystem);
  }
  if (folders_work_current_radio_ != nullptr) {
    folders_work_current_radio_->setChecked(work_mode == WorkDirMode::kCurrent);
  }
  if (folders_work_specified_radio_ != nullptr) {
    folders_work_specified_radio_->setChecked(work_mode == WorkDirMode::kSpecified);
  }
  if (folders_work_path_edit_ != nullptr) {
    folders_work_path_edit_->setText(QDir::toNativeSeparators(normalized_work_path));
    folders_work_path_edit_->setEnabled(work_mode == WorkDirMode::kSpecified);
  }
  if (folders_work_path_browse_button_ != nullptr) {
    folders_work_path_browse_button_->setEnabled(work_mode == WorkDirMode::kSpecified);
  }
  if (folders_work_removable_only_checkbox_ != nullptr) {
    folders_work_removable_only_checkbox_->setChecked(for_removable_only);
  }

  initial_folders_work_mode_ = static_cast<int>(work_mode);
  initial_folders_work_path_ = normalized_work_path;
  initial_folders_work_removable_only_ = for_removable_only;
  folders_settings_dirty_ = false;
  runtime_settings_dirty_ = false;
}

void OptionsDialog::save_runtime_settings() const {
  if (show_dots_checkbox_ == nullptr || show_real_icons_checkbox_ == nullptr ||
      full_row_checkbox_ == nullptr || show_grid_checkbox_ == nullptr ||
      single_click_checkbox_ == nullptr || alternative_selection_checkbox_ == nullptr) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  DisplaySettings display_settings = load_display_settings(settings);
  display_settings.show_dots = show_dots_checkbox_->isChecked();
  display_settings.show_real_file_icons = show_real_icons_checkbox_->isChecked();
  display_settings.full_row_select = full_row_checkbox_->isChecked();
  display_settings.show_grid_lines = show_grid_checkbox_->isChecked();
  display_settings.single_click_open = single_click_checkbox_->isChecked();
  display_settings.alternative_selection_mode =
      alternative_selection_checkbox_->isChecked();
  save_display_settings(settings, display_settings);

  int work_dir_mode = static_cast<int>(WorkDirMode::kSystem);
  if (folders_work_current_radio_ != nullptr && folders_work_current_radio_->isChecked()) {
    work_dir_mode = static_cast<int>(WorkDirMode::kCurrent);
  } else if (folders_work_specified_radio_ != nullptr &&
             folders_work_specified_radio_->isChecked()) {
    work_dir_mode = static_cast<int>(WorkDirMode::kSpecified);
  }
  const QString work_path = folders_work_path_edit_ != nullptr
                                ? QDir::fromNativeSeparators(folders_work_path_edit_->text().trimmed())
                                : QString();
  const bool removable_only = folders_work_removable_only_checkbox_ != nullptr &&
                              folders_work_removable_only_checkbox_->isChecked();
  settings.setValue(QString::fromLatin1(kSettingsFmFoldersWorkDirMode), work_dir_mode);
  settings.setValue(QString::fromLatin1(kSettingsFmFoldersWorkDirPath), work_path);
  settings.setValue(QString::fromLatin1(kSettingsFmFoldersWorkForRemovableOnly),
                    removable_only);
  const_cast<OptionsDialog*>(this)->initial_folders_work_mode_ = work_dir_mode;
  const_cast<OptionsDialog*>(this)->initial_folders_work_path_ = work_path;
  const_cast<OptionsDialog*>(this)->initial_folders_work_removable_only_ = removable_only;
  const_cast<OptionsDialog*>(this)->folders_settings_dirty_ = false;

  if (extract_memory_limit_supported() && mem_limit_enable_checkbox_ != nullptr &&
      mem_limit_spin_ != nullptr) {
    z7::ui::runtime_support::ExtractMemoryLimitSettings mem_settings;
    mem_settings.enabled = mem_limit_enable_checkbox_->isChecked();
    mem_settings.limit_gb = mem_limit_spin_->value();
    z7::ui::runtime_support::save_extract_memory_limit_settings(mem_settings);
  }
  if (large_pages_supported() && use_large_pages_checkbox_ != nullptr) {
    z7::ui::runtime_support::save_large_pages_enabled(
        use_large_pages_checkbox_->isChecked());
    z7::ui::runtime_support::apply_configured_large_pages_mode();
  }
  const_cast<OptionsDialog*>(this)->runtime_settings_dirty_ = false;
}

void OptionsDialog::update_memory_limit_controls_enabled() {
  const bool supported = extract_memory_limit_supported();
  const bool enabled = supported && mem_limit_enable_checkbox_ != nullptr &&
                       mem_limit_enable_checkbox_->isChecked();
  if (mem_usage_label_ != nullptr) {
    mem_usage_label_->setEnabled(supported);
  }
  if (mem_limit_enable_checkbox_ != nullptr) {
    mem_limit_enable_checkbox_->setEnabled(supported);
  }
  if (mem_colon_label_ != nullptr) {
    mem_colon_label_->setEnabled(enabled);
  }
  if (mem_limit_spin_ != nullptr) {
    mem_limit_spin_->setEnabled(enabled);
  }
  if (mem_limit_suffix_label_ != nullptr) {
    mem_limit_suffix_label_->setEnabled(enabled);
  }
}

}  // namespace z7::ui::filemanager
