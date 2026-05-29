// src/ui/filemanager/src/options/layout_core.cpp
// Role: Options dialog lifecycle handlers and main tab setup.

#include "internal.h"

#include <utility>

namespace z7::ui::filemanager {

using namespace options_internal;

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent),
      initial_language_id_(z7::ui::runtime_support::OfficialLangCatalog::instance().current_language()) {
  setup_ui();
  load_editor_settings();
  populate_system_associations();
  populate_context_menu_items();
  load_seven_zip_settings();
  populate_languages();
  load_runtime_settings();
  load_qt_startup_settings();
  update_texts();
  update_language_info();
  load_associations_table_column_widths();
  update_apply_button_state();
}

bool OptionsDialog::language_changed() const {
  return language_changed_;
}

void OptionsDialog::closeEvent(QCloseEvent* event) {
  persist_exit_ui_state_once();
  QDialog::closeEvent(event);
}

void OptionsDialog::on_help_requested() {}

void OptionsDialog::on_accept() {
  persist_exit_ui_state_once();
  on_apply();
  accept();
}

void OptionsDialog::on_reject() {
  persist_exit_ui_state_once();
  reject();
}

void OptionsDialog::on_apply() {
  apply_editor_settings();

  bool changed = false;
  language_changed_ = false;

  const QString selected_language_id = language_combo_ != nullptr
                                           ? language_combo_->currentData(Qt::UserRole).toString()
                                           : QString();
  const QString current_language_id = z7::ui::runtime_support::OfficialLangCatalog::instance().current_language();
  if (!selected_language_id.isEmpty() && selected_language_id != current_language_id) {
    language_changed_ =
        z7::ui::runtime_support::OfficialLangCatalog::instance().set_language_and_persist(selected_language_id);
    if (language_changed_) {
      initial_language_id_ = selected_language_id;
      update_texts();
      update_language_info();
      sync_finder_extension_snapshot_from_options(this);
      changed = true;
    }
  }

  if (runtime_settings_dirty_ || folders_settings_dirty_) {
    save_runtime_settings();
    runtime_settings_dirty_ = false;
    changed = true;
  }
  if (apply_seven_zip_settings()) {
    changed = true;
  }
  if (apply_qt_startup_settings()) {
    changed = true;
  }

  if (changed) {
    emit settings_applied();
  }

  update_apply_button_state();
}

void OptionsDialog::persist_exit_ui_state_once() {
  if (exit_ui_state_persisted_) {
    return;
  }

  save_associations_table_column_widths();
  exit_ui_state_persisted_ = true;
}

void OptionsDialog::on_editor_command_changed() {
  editor_commands_dirty_ =
      viewer_edit_ != nullptr && editor_edit_ != nullptr && diff_edit_ != nullptr &&
      (viewer_edit_->text() != initial_viewer_command_ ||
       editor_edit_->text() != initial_editor_command_ ||
       diff_edit_->text() != initial_diff_command_);
  update_apply_button_state();
}

void OptionsDialog::on_language_selection_changed() {
  update_language_info();
  update_apply_button_state();
}

void OptionsDialog::on_runtime_setting_control_changed() {
  update_memory_limit_controls_enabled();
  runtime_settings_dirty_ = true;
  update_apply_button_state();
}

void OptionsDialog::on_large_pages_toggled(bool checked) {
  if (checked && !z7::ui::runtime_support::probe_large_pages_runtime()) {
    {
      const QSignalBlocker block_large_pages(use_large_pages_checkbox_);
      if (use_large_pages_checkbox_ != nullptr) {
        use_large_pages_checkbox_->setChecked(false);
      }
    }
    QMessageBox::warning(
        this,
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::L(2508)),
        QStringLiteral("macOS could not allocate large memory pages."));
    if (z7::ui::runtime_support::load_large_pages_enabled()) {
      runtime_settings_dirty_ = true;
    }
    update_memory_limit_controls_enabled();
    update_apply_button_state();
    return;
  }
  on_runtime_setting_control_changed();
}

void OptionsDialog::on_qt_setting_control_changed() {
  if (qt_preferred_style_combo_ == nullptr || qt_hidpi_policy_combo_ == nullptr) {
    return;
  }

  const QString preferred = qt_preferred_style_combo_->currentData(Qt::UserRole).toString();
  const Qt::HighDpiScaleFactorRoundingPolicy policy =
      hidpi_policy_from_combo(qt_hidpi_policy_combo_, initial_qt_hidpi_policy_);
  qt_settings_dirty_ = preferred != initial_qt_preferred_style_ ||
                       policy != initial_qt_hidpi_policy_;
  update_apply_button_state();
}

void OptionsDialog::setup_ui() {
  resize(980, 760);

  auto* root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(12, 12, 12, 12);
  root_layout->setSpacing(8);

  tabs_ = new QTabWidget(this);
#ifdef Z7_TESTING
  tabs_->setObjectName(QStringLiteral("optionsTabs"));
#endif

  system_page_ = new QWidget(tabs_);
  seven_zip_page_ = new QWidget(tabs_);
  folders_page_ = create_folders_page();
  editor_page_ = create_editor_page();
  settings_page_ = new QWidget(tabs_);
  build_settings_page();
  qt_page_ = new QWidget(tabs_);
  build_qt_page();
  language_page_ = new QWidget(tabs_);

  auto* system_layout = new QVBoxLayout(system_page_);
  auto* associate_label = new QLabel(system_page_);
  associate_label->setObjectName(QStringLiteral("associateTypesLabel"));
  system_layout->addWidget(associate_label);

  auto* buttons_layout = new QHBoxLayout();
  buttons_layout->addStretch(1);

  current_user_add_button_ = new QPushButton(system_page_);
#ifdef Z7_TESTING
  current_user_add_button_->setObjectName(QStringLiteral("currentUserAddButton"));
#endif
  current_user_add_button_->setMinimumWidth(
      std::max(132, z7::platform::qt::dialog_button_min_width(current_user_add_button_)));
  buttons_layout->addWidget(current_user_add_button_);

  buttons_layout->addSpacing(56);

  all_users_add_button_ = new QPushButton(system_page_);
#ifdef Z7_TESTING
  all_users_add_button_->setObjectName(QStringLiteral("allUsersAddButton"));
#endif
  all_users_add_button_->setMinimumWidth(
      std::max(132, z7::platform::qt::dialog_button_min_width(all_users_add_button_)));
  buttons_layout->addWidget(all_users_add_button_);

  buttons_layout->addStretch(1);
  system_layout->addLayout(buttons_layout);

  associations_table_ = new QTableWidget(system_page_);
#ifdef Z7_TESTING
  associations_table_->setObjectName(QStringLiteral("systemAssociationsTable"));
#endif
  associations_table_->setColumnCount(3);
  associations_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  associations_table_->setSelectionMode(QAbstractItemView::NoSelection);
  associations_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  associations_table_->verticalHeader()->setVisible(false);
  auto* associations_header = associations_table_->horizontalHeader();
  associations_header->setHighlightSections(false);
  associations_header->setStretchLastSection(false);
  associations_header->setSectionResizeMode(QHeaderView::Interactive);
  associations_header->setMinimumSectionSize(column_width_persistence::kMinColumnWidth);
  system_layout->addWidget(associations_table_, 1);

  auto* seven_zip_layout = new QVBoxLayout(seven_zip_page_);
  auto* options_group = new QGroupBox(seven_zip_page_);
#ifdef Z7_TESTING
  options_group->setObjectName(QStringLiteral("sevenZipOptionsGroup"));
#endif
  auto* options_layout = new QVBoxLayout(options_group);

  integrate_shell_checkbox_ = new QCheckBox(options_group);
  integrate_shell_checkbox_->setObjectName(QStringLiteral("integrateShellCheckBox"));
  integrate_shell_checkbox_->setChecked(true);
  options_layout->addWidget(integrate_shell_checkbox_);

  integrate_shell_32_checkbox_ = new QCheckBox(options_group);
  integrate_shell_32_checkbox_->setObjectName(QStringLiteral("integrateShell32CheckBox"));
  integrate_shell_32_checkbox_->setChecked(true);
  options_layout->addWidget(integrate_shell_32_checkbox_);

  cascaded_menu_checkbox_ = new QCheckBox(options_group);
  cascaded_menu_checkbox_->setObjectName(QStringLiteral("cascadedMenuCheckBox"));
  cascaded_menu_checkbox_->setChecked(true);
  options_layout->addWidget(cascaded_menu_checkbox_);

  menu_icons_checkbox_ = new QCheckBox(options_group);
  menu_icons_checkbox_->setObjectName(QStringLiteral("menuIconsCheckBox"));
  menu_icons_checkbox_->setChecked(false);
  options_layout->addWidget(menu_icons_checkbox_);

  eliminate_dup_roots_checkbox_ = new QCheckBox(options_group);
  eliminate_dup_roots_checkbox_->setObjectName(QStringLiteral("eliminateDupRootsCheckBox"));
  eliminate_dup_roots_checkbox_->setChecked(true);
  options_layout->addWidget(eliminate_dup_roots_checkbox_);

  auto* zone_layout = new QHBoxLayout();
  zone_id_label_ = new QLabel(options_group);
  zone_id_label_->setObjectName(QStringLiteral("zoneIdLabel"));
  zone_layout->addWidget(zone_id_label_);
  zone_layout->addStretch(1);

  zone_id_combo_ = new QComboBox(options_group);
  zone_id_combo_->setObjectName(QStringLiteral("zoneIdPolicyCombo"));
  zone_id_combo_->addItem(QStringLiteral("* No (N)"));
  zone_id_combo_->addItem(QStringLiteral("Yes (Y)"));
  zone_id_combo_->addItem(QStringLiteral("Office (O)"));
  zone_layout->addWidget(zone_id_combo_);
  options_layout->addLayout(zone_layout);

  seven_zip_layout->addWidget(options_group);

  auto* context_items_group = new QGroupBox(seven_zip_page_);
  context_items_group->setObjectName(QStringLiteral("contextItemsGroup"));
  auto* context_layout = new QVBoxLayout(context_items_group);

  context_items_list_ = new QListWidget(context_items_group);
#ifdef Z7_TESTING
  context_items_list_->setObjectName(QStringLiteral("contextMenuItemsList"));
#endif
  context_items_list_->setSelectionMode(QAbstractItemView::NoSelection);
  context_layout->addWidget(context_items_list_);

  seven_zip_layout->addWidget(context_items_group, 1);

  auto* language_layout = new QVBoxLayout(language_page_);
  language_label_ = new QLabel(language_page_);
  language_layout->addWidget(language_label_);

  language_combo_ = new QComboBox(language_page_);
#ifdef Z7_TESTING
  language_combo_->setObjectName(QStringLiteral("languageCombo"));
#endif
  language_layout->addWidget(language_combo_);

  language_info_view_ = new QPlainTextEdit(language_page_);
#ifdef Z7_TESTING
  language_info_view_->setObjectName(QStringLiteral("languageInfoText"));
#endif
  language_info_view_->setReadOnly(true);
  language_info_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
  language_layout->addWidget(language_info_view_, 1);

  const bool finder_supported = finder_shell_supported();
  const QString associations_tip = qt_filemanager_unsupported_tooltip();
  const QString finder_tip = finder_shell_tooltip();
  const auto mark_with_tip = [](QWidget* widget, const QString& tip) {
    if (widget == nullptr) {
      return;
    }
    widget->setEnabled(false);
    widget->setToolTip(tip);
  };

  mark_with_tip(associate_label, associations_tip);
  mark_with_tip(current_user_add_button_, associations_tip);
  mark_with_tip(all_users_add_button_, associations_tip);
  mark_with_tip(associations_table_, associations_tip);

  if (!windows_only_supported()) {
    mark_with_tip(integrate_shell_32_checkbox_, windows_only_tooltip());
    mark_with_tip(zone_id_combo_, windows_only_tooltip());
  }

  if (!finder_supported) {
    mark_with_tip(integrate_shell_checkbox_, finder_tip);
    mark_with_tip(cascaded_menu_checkbox_, finder_tip);
    mark_with_tip(menu_icons_checkbox_, finder_tip);
    mark_with_tip(eliminate_dup_roots_checkbox_, finder_tip);
    mark_with_tip(context_items_group, finder_tip);
    mark_with_tip(context_items_list_, finder_tip);
  }

  tabs_->addTab(system_page_, QString());
  tabs_->addTab(seven_zip_page_, QString());
  tabs_->addTab(folders_page_, QString());
  tabs_->addTab(editor_page_, QString());
  tabs_->addTab(settings_page_, QString());
  tabs_->addTab(qt_page_, QString());
  tabs_->addTab(language_page_, QString());

  root_layout->addWidget(tabs_, 1);

  button_box_ = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
          QDialogButtonBox::Help,
      this);
#ifdef Z7_TESTING
  button_box_->setObjectName(QStringLiteral("optionsDialogButtons"));
#endif
  z7::platform::qt::apply_dialog_button_baseline(button_box_);
  root_layout->addWidget(button_box_);

  connect(button_box_, &QDialogButtonBox::accepted, this, &OptionsDialog::on_accept);
  connect(button_box_, &QDialogButtonBox::rejected, this, &OptionsDialog::on_reject);
  connect(button_box_, &QDialogButtonBox::helpRequested, this, &OptionsDialog::on_help_requested);
  connect(language_combo_,
          qOverload<int>(&QComboBox::currentIndexChanged),
          this,
          [this](int) { on_language_selection_changed(); });

  if (integrate_shell_checkbox_ != nullptr) {
    connect(integrate_shell_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_seven_zip_setting_control_changed(); });
  }
  if (integrate_shell_32_checkbox_ != nullptr) {
    connect(integrate_shell_32_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_seven_zip_setting_control_changed(); });
  }
  if (cascaded_menu_checkbox_ != nullptr) {
    connect(cascaded_menu_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_seven_zip_setting_control_changed(); });
  }
  if (menu_icons_checkbox_ != nullptr) {
    connect(menu_icons_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_seven_zip_setting_control_changed(); });
  }
  if (eliminate_dup_roots_checkbox_ != nullptr) {
    connect(eliminate_dup_roots_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool) { on_seven_zip_setting_control_changed(); });
  }
  if (zone_id_combo_ != nullptr) {
    connect(zone_id_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { on_seven_zip_setting_control_changed(); });
  }
  if (context_items_list_ != nullptr) {
    connect(context_items_list_,
            &QListWidget::itemChanged,
            this,
            [this](QListWidgetItem*) { on_seven_zip_setting_control_changed(); });
  }

  if (QPushButton* apply_button = button_box_->button(QDialogButtonBox::Apply)) {
    connect(apply_button, &QPushButton::clicked, this, &OptionsDialog::on_apply);
  }

  update_apply_button_state();
}


}  // namespace z7::ui::filemanager
