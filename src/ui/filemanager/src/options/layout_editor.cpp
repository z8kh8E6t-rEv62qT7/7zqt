// src/ui/filemanager/src/options/layout_editor.cpp
// Role: Editor page, editor settings I/O, and settings page assembly.

#include "internal.h"

namespace z7::ui::filemanager {

using namespace options_internal;

QWidget* OptionsDialog::create_editor_page() {
  auto* page = new QWidget(tabs_);
  auto* layout = new QVBoxLayout(page);

  auto* group = new QGroupBox(page);
#ifdef Z7_TESTING
  group->setObjectName(QStringLiteral("editorSettingsGroup"));
#endif
  auto* group_layout = new QVBoxLayout(group);
  group_layout->setContentsMargins(20, 16, 20, 20);
  group_layout->setSpacing(10);

  auto add_editor_row = [group, group_layout](const QString& label_name,
                                              const QString& edit_name,
                                              const QString& button_name,
                                              QLabel** label_out,
                                              QLineEdit** edit_out,
                                              QPushButton** button_out) {
    auto* label = new QLabel(group);
#ifdef Z7_TESTING
    label->setObjectName(label_name);
#else
    Q_UNUSED(label_name);
#endif
    group_layout->addWidget(label);

    auto* row = new QHBoxLayout();
    auto* edit = new QLineEdit(group);
#ifdef Z7_TESTING
    edit->setObjectName(edit_name);
#else
    Q_UNUSED(edit_name);
#endif
    row->addWidget(edit, 1);

    auto* browse = new QPushButton(QStringLiteral("..."), group);
#ifdef Z7_TESTING
    browse->setObjectName(button_name);
#else
    Q_UNUSED(button_name);
#endif
    browse->setMinimumWidth(64);
    row->addWidget(browse);
    group_layout->addLayout(row);

    if (label_out != nullptr) {
      *label_out = label;
    }
    if (edit_out != nullptr) {
      *edit_out = edit;
    }
    if (button_out != nullptr) {
      *button_out = browse;
    }
  };

  add_editor_row(QStringLiteral("viewerCommandLabel"),
                 QStringLiteral("viewerCommandEdit"),
                 QStringLiteral("viewerBrowseButton"),
                 &viewer_label_,
                 &viewer_edit_,
                 &viewer_browse_button_);
  add_editor_row(QStringLiteral("editorCommandLabel"),
                 QStringLiteral("editorCommandEdit"),
                 QStringLiteral("editorBrowseButton"),
                 &editor_label_,
                 &editor_edit_,
                 &editor_browse_button_);
  add_editor_row(QStringLiteral("diffCommandLabel"),
                 QStringLiteral("diffCommandEdit"),
                 QStringLiteral("diffBrowseButton"),
                 &diff_label_,
                 &diff_edit_,
                 &diff_browse_button_);

  group_layout->addStretch(1);
  layout->addWidget(group, 1);

  if (viewer_edit_ != nullptr) {
    connect(viewer_edit_, &QLineEdit::textChanged, this, &OptionsDialog::on_editor_command_changed);
  }
  if (editor_edit_ != nullptr) {
    connect(editor_edit_, &QLineEdit::textChanged, this, &OptionsDialog::on_editor_command_changed);
  }
  if (diff_edit_ != nullptr) {
    connect(diff_edit_, &QLineEdit::textChanged, this, &OptionsDialog::on_editor_command_changed);
  }

  if (viewer_browse_button_ != nullptr) {
    connect(viewer_browse_button_, &QPushButton::clicked, this, [this]() {
      browse_editor_path(viewer_edit_);
    });
  }
  if (editor_browse_button_ != nullptr) {
    connect(editor_browse_button_, &QPushButton::clicked, this, [this]() {
      browse_editor_path(editor_edit_);
    });
  }
  if (diff_browse_button_ != nullptr) {
    connect(diff_browse_button_, &QPushButton::clicked, this, [this]() {
      browse_editor_path(diff_edit_);
    });
  }

  return page;
}

void OptionsDialog::load_editor_settings() {
  if (viewer_edit_ == nullptr || editor_edit_ == nullptr || diff_edit_ == nullptr) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  initial_viewer_command_ =
      settings.value(QString::fromLatin1(kSettingsFmViewer), QString()).toString();
  initial_editor_command_ =
      settings.value(QString::fromLatin1(kSettingsFmEditor), QString()).toString();
  initial_diff_command_ =
      settings.value(QString::fromLatin1(kSettingsFmDiff), QString()).toString();

  const QSignalBlocker block_viewer(viewer_edit_);
  const QSignalBlocker block_editor(editor_edit_);
  const QSignalBlocker block_diff(diff_edit_);
  viewer_edit_->setText(initial_viewer_command_);
  editor_edit_->setText(initial_editor_command_);
  diff_edit_->setText(initial_diff_command_);
  editor_commands_dirty_ = false;
}

bool OptionsDialog::apply_editor_settings() {
  if (viewer_edit_ == nullptr || editor_edit_ == nullptr || diff_edit_ == nullptr) {
    return false;
  }

  const QString current_viewer = viewer_edit_->text();
  const QString current_editor = editor_edit_->text();
  const QString current_diff = diff_edit_->text();

  const bool viewer_changed = current_viewer != initial_viewer_command_;
  const bool editor_changed = current_editor != initial_editor_command_;
  const bool diff_changed = current_diff != initial_diff_command_;
  if (!viewer_changed && !editor_changed && !diff_changed) {
    editor_commands_dirty_ = false;
    update_apply_button_state();
    return false;
  }

  z7::platform::qt::PortableSettings settings;
  if (viewer_changed) {
    settings.setValue(QString::fromLatin1(kSettingsFmViewer), current_viewer);
  }
  if (editor_changed) {
    settings.setValue(QString::fromLatin1(kSettingsFmEditor), current_editor);
  }
  if (diff_changed) {
    settings.setValue(QString::fromLatin1(kSettingsFmDiff), current_diff);
  }

  initial_viewer_command_ = current_viewer;
  initial_editor_command_ = current_editor;
  initial_diff_command_ = current_diff;
  editor_commands_dirty_ = false;
  update_apply_button_state();
  return true;
}

void OptionsDialog::browse_editor_path(QLineEdit* line_edit) {
  if (line_edit == nullptr) {
    return;
  }
  const QString program_path = command_program_part(line_edit->text());
  const QString selected = QFileDialog::getOpenFileName(this, QString(), program_path);
  if (selected.isEmpty()) {
    return;
  }
  line_edit->setText(rebuild_command_line_with_program(line_edit->text(), selected));
}

void OptionsDialog::build_settings_page() {
  auto* layout = new QVBoxLayout(settings_page_);
  layout->setContentsMargins(24, 12, 24, 12);
  layout->setSpacing(10);

  show_dots_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  show_dots_checkbox_->setObjectName(QStringLiteral("settingsShowDotsCheckBox"));
#endif
  layout->addWidget(show_dots_checkbox_);

  show_real_icons_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  show_real_icons_checkbox_->setObjectName(QStringLiteral("settingsShowRealIconsCheckBox"));
#endif
  layout->addWidget(show_real_icons_checkbox_);

  full_row_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  full_row_checkbox_->setObjectName(QStringLiteral("settingsFullRowCheckBox"));
#endif
  layout->addWidget(full_row_checkbox_);

  show_grid_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  show_grid_checkbox_->setObjectName(QStringLiteral("settingsShowGridCheckBox"));
#endif
  layout->addWidget(show_grid_checkbox_);

  single_click_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  single_click_checkbox_->setObjectName(QStringLiteral("settingsSingleClickCheckBox"));
#endif
  layout->addWidget(single_click_checkbox_);

  alternative_selection_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  alternative_selection_checkbox_->setObjectName(
      QStringLiteral("settingsAlternativeSelectionCheckBox"));
#endif
  layout->addWidget(alternative_selection_checkbox_);

  layout->addSpacing(8);

  show_system_menu_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  show_system_menu_checkbox_->setObjectName(QStringLiteral("settingsShowSystemMenuCheckBox"));
#endif
  // This option stays visible for parity, but the Qt file manager does not
  // implement the upstream system menu integration toggle.
  show_system_menu_checkbox_->setEnabled(false);
  layout->addWidget(show_system_menu_checkbox_);

  layout->addSpacing(8);

  use_large_pages_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  use_large_pages_checkbox_->setObjectName(QStringLiteral("settingsUseLargePagesCheckBox"));
#endif
  use_large_pages_checkbox_->setEnabled(large_pages_supported());
  layout->addWidget(use_large_pages_checkbox_);

  layout->addSpacing(16);

  mem_usage_label_ = new QLabel(settings_page_);
#ifdef Z7_TESTING
  mem_usage_label_->setObjectName(QStringLiteral("settingsMemUsageLabel"));
#endif
  mem_usage_label_->setWordWrap(true);
  layout->addWidget(mem_usage_label_);

  auto* mem_row = new QHBoxLayout();
  mem_row->setContentsMargins(0, 0, 0, 0);
  mem_row->setSpacing(8);

  mem_limit_enable_checkbox_ = new QCheckBox(settings_page_);
#ifdef Z7_TESTING
  mem_limit_enable_checkbox_->setObjectName(QStringLiteral("settingsMemLimitEnabledCheckBox"));
#endif
  mem_row->addWidget(mem_limit_enable_checkbox_);

  mem_colon_label_ = new QLabel(settings_page_);
#ifdef Z7_TESTING
  mem_colon_label_->setObjectName(QStringLiteral("settingsMemColonLabel"));
#endif
  mem_row->addWidget(mem_colon_label_);

  mem_limit_spin_ = new QSpinBox(settings_page_);
#ifdef Z7_TESTING
  mem_limit_spin_->setObjectName(QStringLiteral("settingsMemLimitSpin"));
#endif
  mem_limit_spin_->setMinimum(1);
  mem_limit_spin_->setMaximum(64);
  mem_limit_spin_->setValue(4);
  mem_limit_spin_->setMinimumWidth(160);
  mem_row->addWidget(mem_limit_spin_);

  mem_limit_suffix_label_ = new QLabel(settings_page_);
#ifdef Z7_TESTING
  mem_limit_suffix_label_->setObjectName(QStringLiteral("settingsMemLimitSuffixLabel"));
#endif
  mem_row->addWidget(mem_limit_suffix_label_, 1);

  layout->addLayout(mem_row);
  layout->addStretch(1);

  const QString win_only_tip = windows_only_supported() ? QString() : windows_only_tooltip();
  const bool large_pages_available = large_pages_supported();
  const QString large_pages_tip = large_pages_available ? QString() : large_pages_tooltip();
  const bool mem_supported = extract_memory_limit_supported();
  const QString mem_tip = mem_supported ? QString() : extract_memory_limit_tooltip();
  show_system_menu_checkbox_->setToolTip(win_only_tip);
  use_large_pages_checkbox_->setToolTip(large_pages_tip);
  mem_usage_label_->setToolTip(mem_tip);
  mem_limit_enable_checkbox_->setToolTip(mem_tip);
  mem_colon_label_->setToolTip(mem_tip);
  mem_limit_spin_->setToolTip(mem_tip);
  mem_limit_suffix_label_->setToolTip(mem_tip);

  const quint64 ram_bytes = detect_total_ram_bytes();
  const int max_gb = max_mem_limit_gb(ram_bytes);
  mem_limit_spin_->setMaximum(max_gb);
  mem_limit_spin_->setValue(std::min(4, max_gb));
  mem_limit_suffix_label_->setText(format_mem_suffix(ram_bytes));
  update_memory_limit_controls_enabled();

  const auto implemented_controls = {
      show_dots_checkbox_,
      show_real_icons_checkbox_,
      full_row_checkbox_,
      show_grid_checkbox_,
      single_click_checkbox_,
      alternative_selection_checkbox_,
      mem_limit_enable_checkbox_};
  for (QCheckBox* checkbox : implemented_controls) {
    if (checkbox == nullptr) {
      continue;
    }
    connect(checkbox, &QCheckBox::toggled, this, &OptionsDialog::on_runtime_setting_control_changed);
  }
  connect(use_large_pages_checkbox_,
          &QCheckBox::toggled,
          this,
          &OptionsDialog::on_large_pages_toggled);
  connect(mem_limit_spin_,
          qOverload<int>(&QSpinBox::valueChanged),
          this,
          [this](int) { on_runtime_setting_control_changed(); });
}

}  // namespace z7::ui::filemanager
