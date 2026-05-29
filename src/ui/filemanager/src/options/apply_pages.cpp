// src/ui/filemanager/src/options/apply_pages.cpp
// Role: Qt page construction and runtime settings load/save.

#include "internal.h"

#include "shell_integration_menu.h"

#include <cstdint>

#if defined(Q_OS_MAC)
#include "macos_integration_config.h"
#endif

namespace z7::ui::filemanager {

using namespace options_internal;

namespace {

namespace shell = z7::shell_integration;

struct ContextMenuOptionItem {
  QString action_id;
  QString label;
  bool supported = true;
};

QVector<bool> capture_context_item_checks(const QListWidget* list) {
  QVector<bool> checks;
  if (list == nullptr) {
    return checks;
  }
  checks.reserve(list->count());
  for (int i = 0; i < list->count(); ++i) {
    const QListWidgetItem* item = list->item(i);
    checks.push_back(item != nullptr && item->checkState() == Qt::Checked);
  }
  return checks;
}

QStringList capture_context_visible_actions(const QListWidget* list) {
  QStringList actions;
  if (list == nullptr) {
    return actions;
  }
  actions.reserve(list->count());
  for (int i = 0; i < list->count(); ++i) {
    const QListWidgetItem* item = list->item(i);
    if (item == nullptr || item->checkState() != Qt::Checked) {
      continue;
    }
    const QString action_id = item->data(Qt::UserRole).toString().trimmed();
    if (!action_id.isEmpty()) {
      actions.push_back(action_id);
    }
  }
  return shell::normalize_shell_integration_visible_actions(actions);
}

std::uint32_t context_menu_flags_from_variant(const QVariant& value) {
  bool ok = false;
  const qulonglong raw = value.toULongLong(&ok);
  return ok ? static_cast<std::uint32_t>(raw) : 0;
}

QString combine_tooltips(const QString& first, const QString& second) {
  const QString left = first.trimmed();
  const QString right = second.trimmed();
  if (left.isEmpty()) {
    return right;
  }
  if (right.isEmpty()) {
    return left;
  }
  return left + QLatin1Char('\n') + right;
}

}  // namespace

namespace options_internal {

void sync_finder_extension_snapshot_from_options(QWidget* parent) {
#if defined(Q_OS_MAC)
  QString snapshot_error;
  if (!z7::macos_integration::sync_macos_integration_config_snapshot_from_settings(
          &snapshot_error) &&
      !snapshot_error.trimmed().isEmpty()) {
    QMessageBox::warning(parent,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2100)),
                         QStringLiteral("Failed to update Finder extension snapshot:\n%1")
                             .arg(snapshot_error));
  }
#else
  Q_UNUSED(parent);
#endif
}

}  // namespace options_internal

void OptionsDialog::build_qt_page() {
  auto* layout = new QVBoxLayout(qt_page_);
  layout->setContentsMargins(24, 12, 24, 12);
  layout->setSpacing(12);

  auto* startup_group = new QGroupBox(qt_page_);
  startup_group->setObjectName(QStringLiteral("qtStartupGroup"));
  auto* group_layout = new QVBoxLayout(startup_group);
  group_layout->setContentsMargins(16, 12, 16, 12);
  group_layout->setSpacing(10);

  auto add_combo_row = [startup_group, group_layout](const QString& label_name,
                                                     const QString& combo_name,
                                                     QComboBox** combo_out) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    auto* label = new QLabel(startup_group);
    label->setObjectName(label_name);
    label->setMinimumWidth(180);
    row->addWidget(label);

    auto* combo = new QComboBox(startup_group);
#ifdef Z7_TESTING
    combo->setObjectName(combo_name);
#else
    Q_UNUSED(combo_name);
#endif
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    row->addWidget(combo, 1);
    group_layout->addLayout(row);

    if (combo_out != nullptr) {
      *combo_out = combo;
    }
  };

  add_combo_row(QStringLiteral("qtPreferredStyleLabel"),
                QStringLiteral("qtPreferredStyleCombo"),
                &qt_preferred_style_combo_);
  add_combo_row(QStringLiteral("qtHiDpiPolicyLabel"),
                QStringLiteral("qtHiDpiPolicyCombo"),
                &qt_hidpi_policy_combo_);

  const QStringList styles = z7::platform::qt::available_qt_styles();
  if (qt_preferred_style_combo_ != nullptr) {
    qt_preferred_style_combo_->addItem(QStringLiteral("Fusion (default)"),
                                       QStringLiteral("Fusion"));
    qt_preferred_style_combo_->addItem(QStringLiteral("System default"), QString());
    for (const QString& style : styles) {
      if (style.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) == 0) {
        continue;
      }
      qt_preferred_style_combo_->addItem(style, style);
    }
  }

  if (qt_hidpi_policy_combo_ != nullptr) {
    add_hidpi_policy_combo_item(qt_hidpi_policy_combo_,
                                QStringLiteral("PassThrough (default)"),
                                Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    add_hidpi_policy_combo_item(qt_hidpi_policy_combo_,
                                QStringLiteral("Round"),
                                Qt::HighDpiScaleFactorRoundingPolicy::Round);
    add_hidpi_policy_combo_item(qt_hidpi_policy_combo_,
                                QStringLiteral("RoundPreferFloor"),
                                Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
    add_hidpi_policy_combo_item(qt_hidpi_policy_combo_,
                                QStringLiteral("Ceil"),
                                Qt::HighDpiScaleFactorRoundingPolicy::Ceil);
    add_hidpi_policy_combo_item(qt_hidpi_policy_combo_,
                                QStringLiteral("Floor"),
                                Qt::HighDpiScaleFactorRoundingPolicy::Floor);
  }

  qt_restart_hint_label_ = new QLabel(startup_group);
#ifdef Z7_TESTING
  qt_restart_hint_label_->setObjectName(QStringLiteral("qtRestartHintLabel"));
#endif
  qt_restart_hint_label_->setWordWrap(true);
  group_layout->addWidget(qt_restart_hint_label_);

  layout->addWidget(startup_group);
  layout->addStretch(1);

  if (qt_preferred_style_combo_ != nullptr) {
    connect(qt_preferred_style_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { on_qt_setting_control_changed(); });
  }
  if (qt_hidpi_policy_combo_ != nullptr) {
    connect(qt_hidpi_policy_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { on_qt_setting_control_changed(); });
  }
}

void OptionsDialog::populate_system_associations() {
  associations_table_->setRowCount(association_extensions().size());

  for (int row = 0; row < association_extensions().size(); ++row) {
    auto* type_item = new QTableWidgetItem(association_extensions().at(row));
    auto* current_user_item = new QTableWidgetItem(QString());
    auto* all_users_item = new QTableWidgetItem(QString());

    type_item->setFlags(type_item->flags() & ~Qt::ItemIsEditable);
    current_user_item->setFlags(current_user_item->flags() & ~Qt::ItemIsEditable);
    all_users_item->setFlags(all_users_item->flags() & ~Qt::ItemIsEditable);

    associations_table_->setItem(row, 0, type_item);
    associations_table_->setItem(row, 1, current_user_item);
    associations_table_->setItem(row, 2, all_users_item);
  }
}

void OptionsDialog::populate_context_menu_items() {
  context_items_list_->clear();

  const QString folder_label = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2320));
  const QString archive_label = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2321));
  const auto label_for_action = [&](const QString& action_id) -> QString {
    if (action_id == QString::fromLatin1(shell::kActionOpen)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2322));
    }
    if (action_id == QString::fromLatin1(shell::kActionOpenAsMenu)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2322)) + QStringLiteral(" >");
    }
    if (action_id == QString::fromLatin1(shell::kActionExtractFiles)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2323));
    }
    if (action_id == QString::fromLatin1(shell::kActionExtractHere)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2326));
    }
    if (action_id == QString::fromLatin1(shell::kActionExtractTo)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::LF(2327, {folder_label}));
    }
    if (action_id == QString::fromLatin1(shell::kActionTestArchive)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2325));
    }
    if (action_id == QString::fromLatin1(shell::kActionAddToArchive)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2324));
    }
    if (action_id == QString::fromLatin1(shell::kActionAddTo7z)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::LF(2328, {archive_label + QStringLiteral(".7z")}));
    }
    if (action_id == QString::fromLatin1(shell::kActionAddToZip)) {
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::LF(2328, {archive_label + QStringLiteral(".zip")}));
    }
    if (action_id == QString::fromLatin1(shell::kActionCrcShaMenu)) {
      return z7::ui::runtime_support::J(
                 QStringLiteral("shell.actions.crc_sha_menu")) +
             QStringLiteral(" >");
    }
    return action_id;
  };

  const QString unsupported_tip = combine_tooltips(
      finder_shell_supported() ? QString() : finder_shell_tooltip(),
      qt_filemanager_unsupported_tooltip());
  QList<ContextMenuOptionItem> option_items;
  option_items.reserve(shell::shell_integration_context_menu_action_ids().size() + 3);
  const auto push_option_item = [&option_items](const QString& action_id,
                                                const QString& label,
                                                bool supported) {
    ContextMenuOptionItem option;
    option.action_id = action_id;
    option.label = label;
    option.supported = supported;
    option_items.push_back(option);
  };

  for (const QString& action_id : shell::shell_integration_context_menu_action_ids()) {
    if (action_id == QString::fromLatin1(shell::kActionCrcShaMenu)) {
      push_option_item(
          QString(),
          z7::ui::runtime_support::strip_mnemonic(
              z7::ui::runtime_support::L(2329)) +
              unsupported_suffix(),
          false);
      push_option_item(
          QString(),
          z7::ui::runtime_support::strip_mnemonic(
              z7::ui::runtime_support::LF(
                  2330, {archive_label + QStringLiteral(".7z")})) +
              unsupported_suffix(),
          false);
      push_option_item(
          QString(),
          z7::ui::runtime_support::strip_mnemonic(
              z7::ui::runtime_support::LF(
                  2330, {archive_label + QStringLiteral(".zip")})) +
              unsupported_suffix(),
          false);
    }
    push_option_item(action_id,
                     with_finder_shell_suffix_if_unsupported(
                         label_for_action(action_id)),
                     true);
  }

  for (const ContextMenuOptionItem& option : option_items) {
    auto* item = new QListWidgetItem(option.label, context_items_list_);
    item->setData(Qt::UserRole, option.action_id);
    if (option.supported) {
      item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsSelectable);
      item->setCheckState(Qt::Checked);
      continue;
    }
    item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                     Qt::ItemIsUserCheckable));
    item->setCheckState(Qt::Unchecked);
    item->setToolTip(unsupported_tip);
  }
}

void OptionsDialog::load_seven_zip_settings() {
  if (integrate_shell_checkbox_ == nullptr ||
      integrate_shell_32_checkbox_ == nullptr ||
      cascaded_menu_checkbox_ == nullptr ||
      menu_icons_checkbox_ == nullptr ||
      eliminate_dup_roots_checkbox_ == nullptr ||
      zone_id_combo_ == nullptr ||
      context_items_list_ == nullptr) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  initial_integrate_shell_ =
      settings.value(QString::fromLatin1(kSettingsOptionsIntegrateShell), true).toBool();
  initial_integrate_shell_32_ =
      settings.value(QString::fromLatin1(kSettingsOptionsIntegrateShell32), true).toBool();
  initial_cascaded_menu_ =
      settings.value(QString::fromLatin1(kSettingsOptionsCascadedMenu), true).toBool();
  initial_menu_icons_ =
      settings.value(QString::fromLatin1(kSettingsOptionsMenuIcons), false).toBool();
  initial_eliminate_dup_roots_ =
      settings.value(QString::fromLatin1(kSettingsOptionsElimDupExtract), true).toBool();
  initial_zone_id_policy_index_ =
      settings.value(QString::fromLatin1(kSettingsOptionsWriteZoneIdExtract), 0).toInt();
  if (initial_zone_id_policy_index_ < 0 ||
      initial_zone_id_policy_index_ >= zone_id_combo_->count()) {
    initial_zone_id_policy_index_ = 0;
  }

  const QString context_menu_key =
      QString::fromLatin1(kSettingsOptionsContextMenu);
  const QStringList visible_actions =
      settings.contains(context_menu_key)
          ? shell::shell_integration_visible_actions_from_context_menu_flags(
                context_menu_flags_from_variant(settings.value(context_menu_key)))
          : shell::default_shell_integration_visible_actions();

  const QSignalBlocker block_shell(integrate_shell_checkbox_);
  const QSignalBlocker block_shell32(integrate_shell_32_checkbox_);
  const QSignalBlocker block_cascaded(cascaded_menu_checkbox_);
  const QSignalBlocker block_icons(menu_icons_checkbox_);
  const QSignalBlocker block_elim(eliminate_dup_roots_checkbox_);
  const QSignalBlocker block_zone(zone_id_combo_);
  const QSignalBlocker block_context(context_items_list_);

  integrate_shell_checkbox_->setChecked(initial_integrate_shell_);
  integrate_shell_32_checkbox_->setChecked(initial_integrate_shell_32_);
  cascaded_menu_checkbox_->setChecked(initial_cascaded_menu_);
  menu_icons_checkbox_->setChecked(initial_menu_icons_);
  eliminate_dup_roots_checkbox_->setChecked(initial_eliminate_dup_roots_);
  zone_id_combo_->setCurrentIndex(initial_zone_id_policy_index_);

  for (int i = 0; i < context_items_list_->count(); ++i) {
    if (QListWidgetItem* item = context_items_list_->item(i)) {
      const QString action_id = item->data(Qt::UserRole).toString();
      item->setCheckState(visible_actions.contains(action_id) ? Qt::Checked
                                                              : Qt::Unchecked);
    }
  }
  initial_context_item_checks_ = capture_context_item_checks(context_items_list_);

  seven_zip_settings_dirty_ = false;
}

bool OptionsDialog::apply_seven_zip_settings() {
  if (integrate_shell_checkbox_ == nullptr ||
      integrate_shell_32_checkbox_ == nullptr ||
      cascaded_menu_checkbox_ == nullptr ||
      menu_icons_checkbox_ == nullptr ||
      eliminate_dup_roots_checkbox_ == nullptr ||
      zone_id_combo_ == nullptr ||
      context_items_list_ == nullptr) {
    return false;
  }

  const bool current_integrate_shell = integrate_shell_checkbox_->isChecked();
  const bool current_integrate_shell_32 = integrate_shell_32_checkbox_->isChecked();
  const bool current_cascaded_menu = cascaded_menu_checkbox_->isChecked();
  const bool current_menu_icons = menu_icons_checkbox_->isChecked();
  const bool current_eliminate_dup_roots = eliminate_dup_roots_checkbox_->isChecked();
  const int current_zone_policy = zone_id_combo_->currentIndex();
  const QVector<bool> current_checks = capture_context_item_checks(context_items_list_);

  const bool changed =
      current_integrate_shell != initial_integrate_shell_ ||
      current_integrate_shell_32 != initial_integrate_shell_32_ ||
      current_cascaded_menu != initial_cascaded_menu_ ||
      current_menu_icons != initial_menu_icons_ ||
      current_eliminate_dup_roots != initial_eliminate_dup_roots_ ||
      current_zone_policy != initial_zone_id_policy_index_ ||
      current_checks != initial_context_item_checks_;
  if (!changed) {
    seven_zip_settings_dirty_ = false;
    return false;
  }

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QString::fromLatin1(kSettingsOptionsIntegrateShell), current_integrate_shell);
  settings.setValue(QString::fromLatin1(kSettingsOptionsIntegrateShell32),
                    current_integrate_shell_32);
  settings.setValue(QString::fromLatin1(kSettingsOptionsCascadedMenu), current_cascaded_menu);
  settings.setValue(QString::fromLatin1(kSettingsOptionsMenuIcons), current_menu_icons);
  settings.setValue(QString::fromLatin1(kSettingsOptionsElimDupExtract),
                    current_eliminate_dup_roots);
  settings.setValue(QString::fromLatin1(kSettingsOptionsWriteZoneIdExtract), current_zone_policy);
  settings.setValue(
      QString::fromLatin1(kSettingsOptionsContextMenu),
      QVariant::fromValue(static_cast<qulonglong>(
          shell::shell_integration_context_menu_flags_from_visible_actions(
              capture_context_visible_actions(context_items_list_)))));

  sync_finder_extension_snapshot_from_options(this);

  initial_integrate_shell_ = current_integrate_shell;
  initial_integrate_shell_32_ = current_integrate_shell_32;
  initial_cascaded_menu_ = current_cascaded_menu;
  initial_menu_icons_ = current_menu_icons;
  initial_eliminate_dup_roots_ = current_eliminate_dup_roots;
  initial_zone_id_policy_index_ = current_zone_policy;
  initial_context_item_checks_ = current_checks;
  seven_zip_settings_dirty_ = false;
  return true;
}

void OptionsDialog::on_seven_zip_setting_control_changed() {
  if (integrate_shell_checkbox_ == nullptr ||
      integrate_shell_32_checkbox_ == nullptr ||
      cascaded_menu_checkbox_ == nullptr ||
      menu_icons_checkbox_ == nullptr ||
      eliminate_dup_roots_checkbox_ == nullptr ||
      zone_id_combo_ == nullptr ||
      context_items_list_ == nullptr) {
    return;
  }

  const QVector<bool> current_checks = capture_context_item_checks(context_items_list_);
  seven_zip_settings_dirty_ =
      integrate_shell_checkbox_->isChecked() != initial_integrate_shell_ ||
      integrate_shell_32_checkbox_->isChecked() != initial_integrate_shell_32_ ||
      cascaded_menu_checkbox_->isChecked() != initial_cascaded_menu_ ||
      menu_icons_checkbox_->isChecked() != initial_menu_icons_ ||
      eliminate_dup_roots_checkbox_->isChecked() != initial_eliminate_dup_roots_ ||
      zone_id_combo_->currentIndex() != initial_zone_id_policy_index_ ||
      current_checks != initial_context_item_checks_;
  update_apply_button_state();
}

void OptionsDialog::populate_languages() {
  language_combo_->clear();

  const QList<z7::ui::runtime_support::LangInfo> langs = z7::ui::runtime_support::OfficialLangCatalog::instance().available_languages();
  int current_index = 0;

  for (const z7::ui::runtime_support::LangInfo& lang : langs) {
    QString label = lang.english_name;
    if (!lang.native_name.isEmpty() && lang.native_name != lang.english_name) {
      label += QStringLiteral(" (") + lang.native_name + QStringLiteral(")");
    }
    if (!lang.mark.isEmpty()) {
      label += QStringLiteral("  ") + lang.mark;
    }

    language_combo_->addItem(label, lang.id);
    if (lang.id == initial_language_id_) {
      current_index = language_combo_->count() - 1;
    }
  }

  language_combo_->setCurrentIndex(current_index);
}

}  // namespace z7::ui::filemanager
