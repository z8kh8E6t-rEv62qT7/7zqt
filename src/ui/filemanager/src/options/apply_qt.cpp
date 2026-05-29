// src/ui/filemanager/src/options/apply_qt.cpp
// Role: Qt startup settings apply flow and table-width persistence.

#include "internal.h"

namespace z7::ui::filemanager {

using namespace options_internal;

void OptionsDialog::load_qt_startup_settings() {
  if (qt_preferred_style_combo_ == nullptr || qt_hidpi_policy_combo_ == nullptr) {
    return;
  }

  const z7::platform::qt::AppStartupConfig config =
      z7::platform::qt::startup_config_with_persisted_overrides(
          z7::platform::qt::StartupAppKind::kFileManager);
  initial_qt_preferred_style_ = config.preferred_style;
  initial_qt_hidpi_policy_ = config.hidpi.scale_factor_rounding;

  const QSignalBlocker block_preferred(qt_preferred_style_combo_);
  const QSignalBlocker block_hidpi(qt_hidpi_policy_combo_);

  select_combo_value_or_insert(qt_preferred_style_combo_,
                               initial_qt_preferred_style_,
                               QStringLiteral(" (currently unavailable)"));

  int hidpi_index =
      qt_hidpi_policy_combo_->findData(static_cast<int>(initial_qt_hidpi_policy_), Qt::UserRole);
  if (hidpi_index < 0) {
    hidpi_index = qt_hidpi_policy_combo_->findData(
        static_cast<int>(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough), Qt::UserRole);
  }
  if (hidpi_index < 0) {
    hidpi_index = 0;
  }
  qt_hidpi_policy_combo_->setCurrentIndex(hidpi_index);

  qt_settings_dirty_ = false;
}

bool OptionsDialog::apply_qt_startup_settings() {
  if (qt_preferred_style_combo_ == nullptr || qt_hidpi_policy_combo_ == nullptr) {
    return false;
  }

  const QString preferred = qt_preferred_style_combo_->currentData(Qt::UserRole).toString();
  const Qt::HighDpiScaleFactorRoundingPolicy hidpi_policy =
      hidpi_policy_from_combo(qt_hidpi_policy_combo_, initial_qt_hidpi_policy_);

  const bool changed = preferred != initial_qt_preferred_style_ ||
                       hidpi_policy != initial_qt_hidpi_policy_;
  if (!changed) {
    qt_settings_dirty_ = false;
    return false;
  }

  z7::platform::qt::AppStartupConfig startup =
      z7::platform::qt::default_startup_config(
          z7::platform::qt::StartupAppKind::kFileManager);
  startup.preferred_style = preferred;
  startup.hidpi.scale_factor_rounding = hidpi_policy;
  z7::platform::qt::persist_startup_overrides(startup);

  if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
    z7::platform::qt::apply_post_app_startup(*app, startup);
  }

  initial_qt_preferred_style_ = preferred;
  initial_qt_hidpi_policy_ = hidpi_policy;
  qt_settings_dirty_ = false;
  return true;
}

void OptionsDialog::update_language_info() {
  if (language_combo_ == nullptr || language_info_view_ == nullptr) {
    return;
  }

  const QString selected_id = language_combo_->currentData(Qt::UserRole).toString();
  const QList<z7::ui::runtime_support::LangInfo> langs = z7::ui::runtime_support::OfficialLangCatalog::instance().available_languages();

  for (const z7::ui::runtime_support::LangInfo& lang : langs) {
    if (lang.id != selected_id) {
      continue;
    }

    QStringList lines;
    lines << format_language_summary_line(lang);

    for (const QString& comment : lang.comments) {
      if (!comment.trimmed().isEmpty()) {
        lines << comment;
      }
    }

    append_limited_lines(&lines, lang.missing_lines, QStringLiteral("Missing lines"));
    append_limited_lines(&lines, lang.extra_lines, QStringLiteral("Extra lines"));

    language_info_view_->setPlainText(lines.join(QLatin1Char('\n')));
    return;
  }

  language_info_view_->clear();
}

void OptionsDialog::update_apply_button_state() {
  if (button_box_ == nullptr) {
    return;
  }
  const QString selected_language_id = language_combo_ != nullptr
                                           ? language_combo_->currentData(Qt::UserRole).toString()
                                           : QString();
  const bool language_dirty =
      !selected_language_id.isEmpty() &&
      selected_language_id != z7::ui::runtime_support::OfficialLangCatalog::instance().current_language();
  if (QPushButton* apply_button = button_box_->button(QDialogButtonBox::Apply)) {
    apply_button->setEnabled(runtime_settings_dirty_ || editor_commands_dirty_ ||
                             seven_zip_settings_dirty_ || folders_settings_dirty_ ||
                             qt_settings_dirty_ || language_dirty);
  }
}

void OptionsDialog::load_associations_table_column_widths() {
  if (associations_table_ == nullptr || associations_table_->horizontalHeader() == nullptr) {
    return;
  }

  constexpr int kExpectedColumns = 3;
  const QVector<int> default_widths = {120, 220, 220};

  z7::platform::qt::PortableSettings settings;
  const QString encoded = settings
                              .value(QString::fromLatin1(kSettingsFmOptionsAssociationsColumns),
                                     QString())
                              .toString()
                              .trimmed();

  QVector<int> widths;
  if (!column_width_persistence::decode_widths(encoded, kExpectedColumns, &widths)) {
    widths = default_widths;
  }
  column_width_persistence::apply_widths(associations_table_->horizontalHeader(), widths);
}

void OptionsDialog::save_associations_table_column_widths() const {
  if (associations_table_ == nullptr || associations_table_->horizontalHeader() == nullptr) {
    return;
  }

  constexpr int kExpectedColumns = 3;
  const QVector<int> widths = column_width_persistence::capture_widths(
      associations_table_->horizontalHeader(), kExpectedColumns);
  if (widths.size() != kExpectedColumns) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  settings.setValue(
      QString::fromLatin1(kSettingsFmOptionsAssociationsColumns),
      column_width_persistence::encode_widths(widths));
}


}  // namespace z7::ui::filemanager
