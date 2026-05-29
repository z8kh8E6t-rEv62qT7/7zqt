// src/ui/filemanager/src/options/apply_text.cpp
// Role: Text refresh helpers.

#include "internal.h"

namespace z7::ui::filemanager {

using namespace options_internal;

void OptionsDialog::update_texts() {
  const bool finder_supported = finder_shell_supported();
  const bool mem_supported = extract_memory_limit_supported();
  const bool large_pages_available = large_pages_supported();
  const QString win_only_tip = windows_only_tooltip();
  const QString win_only_tip_or_empty = windows_only_supported() ? QString() : win_only_tip;
  const QString finder_tip = finder_shell_tooltip();
  const QString finder_tip_or_empty = finder_supported ? QString() : finder_tip;
  const QString mem_tip = extract_memory_limit_tooltip();
  const QString mem_tip_or_empty = mem_supported ? QString() : mem_tip;
  const QString large_pages_tip = large_pages_tooltip();
  const QString large_pages_tip_or_empty =
      large_pages_available ? QString() : large_pages_tip;
  const QString associations_tip = qt_filemanager_unsupported_tooltip();
  const QString show_system_menu_tip = [&]() {
    if (win_only_tip_or_empty.isEmpty()) {
      return qt_filemanager_unsupported_tooltip();
    }
    return win_only_tip_or_empty + QLatin1Char('\n') +
           qt_filemanager_unsupported_tooltip();
  }();
  setWindowTitle(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2100)));

  tabs_->setTabText(0, with_windows_only_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2200))));
  tabs_->setTabText(1, with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(0))));
  tabs_->setTabText(2, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2400)));
  tabs_->setTabText(3, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2103)));
  tabs_->setTabText(4, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2500)));
  tabs_->setTabText(5, QStringLiteral("Qt"));
  tabs_->setTabText(6, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2101)));

  if (auto* label = findChild<QLabel*>(QStringLiteral("associateTypesLabel"))) {
    label->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2201)));
    label->setToolTip(associations_tip);
  }

  current_user_add_button_->setText(QStringLiteral("+"));
  all_users_add_button_->setText(QStringLiteral("+"));

  associations_table_->setHorizontalHeaderLabels(
      {z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1020)), current_user_label(), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2202))});

  if (auto* integrate_shell = findChild<QCheckBox*>(QStringLiteral("integrateShellCheckBox"))) {
    integrate_shell->setText(with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2301))));
    integrate_shell->setToolTip(finder_tip_or_empty);
  }
  if (auto* integrate_shell_32 =
          findChild<QCheckBox*>(QStringLiteral("integrateShell32CheckBox"))) {
    integrate_shell_32->setText(with_windows_only_suffix_if_unsupported(
        QStringLiteral("Integrate 7-Zip into shell context menu (32-bit)")));
    integrate_shell_32->setToolTip(win_only_tip_or_empty);
  }
  if (auto* cascaded_menu = findChild<QCheckBox*>(QStringLiteral("cascadedMenuCheckBox"))) {
    cascaded_menu->setText(with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2302))));
    cascaded_menu->setToolTip(finder_tip_or_empty);
  }
  if (auto* menu_icons = findChild<QCheckBox*>(QStringLiteral("menuIconsCheckBox"))) {
    menu_icons->setText(with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2304))));
    menu_icons->setToolTip(finder_tip_or_empty);
  }
  if (auto* eliminate_dup_roots =
          findChild<QCheckBox*>(QStringLiteral("eliminateDupRootsCheckBox"))) {
    eliminate_dup_roots->setText(
        with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3430))));
    eliminate_dup_roots->setToolTip(finder_tip_or_empty);
  }
  if (auto* zone_label = findChild<QLabel*>(QStringLiteral("zoneIdLabel"))) {
    zone_label->setText(with_windows_only_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3440))));
    zone_label->setToolTip(win_only_tip_or_empty);
  }
  if (auto* zone_combo = findChild<QComboBox*>(QStringLiteral("zoneIdPolicyCombo"))) {
    zone_combo->setToolTip(win_only_tip_or_empty);
  }
  if (auto* group = findChild<QGroupBox*>(QStringLiteral("contextItemsGroup"))) {
    group->setTitle(with_finder_shell_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2303))));
    group->setToolTip(finder_tip_or_empty);
  }
  if (current_user_add_button_ != nullptr) {
    current_user_add_button_->setToolTip(associations_tip);
  }
  if (all_users_add_button_ != nullptr) {
    all_users_add_button_->setToolTip(associations_tip);
  }
  if (associations_table_ != nullptr) {
    associations_table_->setToolTip(associations_tip);
  }
  if (context_items_list_ != nullptr) {
    context_items_list_->setToolTip(finder_tip_or_empty);
  }

  if (show_dots_checkbox_ != nullptr) {
    show_dots_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2501)));
  }
  if (show_real_icons_checkbox_ != nullptr) {
    show_real_icons_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2502)));
  }
  if (full_row_checkbox_ != nullptr) {
    full_row_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2504)));
  }
  if (show_grid_checkbox_ != nullptr) {
    show_grid_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2505)));
  }
  if (single_click_checkbox_ != nullptr) {
    single_click_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2506)));
  }
  if (alternative_selection_checkbox_ != nullptr) {
    alternative_selection_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2507)));
  }
  if (show_system_menu_checkbox_ != nullptr) {
    show_system_menu_checkbox_->setText(
        with_windows_only_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2503))));
    show_system_menu_checkbox_->setToolTip(show_system_menu_tip);
  }
  if (use_large_pages_checkbox_ != nullptr) {
    use_large_pages_checkbox_->setText(
        with_large_pages_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2508))));
    use_large_pages_checkbox_->setToolTip(large_pages_tip_or_empty);
  }
  if (mem_usage_label_ != nullptr) {
    mem_usage_label_->setText(
        with_extract_memory_limit_suffix_if_unsupported(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7816))));
    mem_usage_label_->setToolTip(mem_tip_or_empty);
  }
  if (mem_colon_label_ != nullptr) {
    mem_colon_label_->setText(QStringLiteral(":"));
  }
  if (mem_limit_suffix_label_ != nullptr) {
    mem_limit_suffix_label_->setText(format_mem_suffix(detect_total_ram_bytes()));
    mem_limit_suffix_label_->setToolTip(mem_tip_or_empty);
  }
  if (mem_limit_enable_checkbox_ != nullptr) {
    mem_limit_enable_checkbox_->setToolTip(mem_tip_or_empty);
  }
  if (mem_colon_label_ != nullptr) {
    mem_colon_label_->setToolTip(mem_tip_or_empty);
  }
  if (mem_limit_spin_ != nullptr) {
    mem_limit_spin_->setToolTip(mem_tip_or_empty);
  }

  if (auto* group = findChild<QGroupBox*>(QStringLiteral("qtStartupGroup"))) {
    group->setTitle(z7::ui::runtime_support::J(
        QStringLiteral("ui.options.qt_startup.group_title")));
  }
  if (auto* label = findChild<QLabel*>(QStringLiteral("qtPreferredStyleLabel"))) {
    label->setText(z7::ui::runtime_support::J(
        QStringLiteral("ui.options.qt_startup.preferred_style_label")));
  }
  if (auto* label = findChild<QLabel*>(QStringLiteral("qtHiDpiPolicyLabel"))) {
    label->setText(z7::ui::runtime_support::J(
        QStringLiteral("ui.options.qt_startup.hidpi_policy_label")));
  }
  if (qt_restart_hint_label_ != nullptr) {
    qt_restart_hint_label_->setText(z7::ui::runtime_support::J(
        QStringLiteral("ui.options.qt_startup.restart_hint")));
  }

  if (viewer_label_ != nullptr) {
    viewer_label_->setText(ensure_colon_suffix(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(543))));
  }
  if (editor_label_ != nullptr) {
    editor_label_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2104)));
  }
  if (diff_label_ != nullptr) {
    diff_label_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2105)));
  }
  if (viewer_browse_button_ != nullptr) {
    viewer_browse_button_->setText(QStringLiteral("..."));
  }
  if (editor_browse_button_ != nullptr) {
    editor_browse_button_->setText(QStringLiteral("..."));
  }
  if (diff_browse_button_ != nullptr) {
    diff_browse_button_->setText(QStringLiteral("..."));
  }

  if (folders_working_folder_label_ != nullptr) {
    folders_working_folder_label_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2401)));
  }
  if (folders_work_system_radio_ != nullptr) {
    folders_work_system_radio_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2402)));
  }
  if (folders_work_current_radio_ != nullptr) {
    folders_work_current_radio_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2403)));
  }
  if (folders_work_specified_radio_ != nullptr) {
    folders_work_specified_radio_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2404)));
  }
  if (folders_work_removable_only_checkbox_ != nullptr) {
    folders_work_removable_only_checkbox_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2405)));
  }
  if (folders_work_path_browse_button_ != nullptr) {
    folders_work_path_browse_button_->setText(QStringLiteral("..."));
  }

  if (language_label_ != nullptr) {
    language_label_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2102)));
  }

  if (QPushButton* ok_button = button_box_->button(QDialogButtonBox::Ok)) {
    ok_button->setText(z7::ui::runtime_support::L(401));
  }
  if (QPushButton* cancel_button = button_box_->button(QDialogButtonBox::Cancel)) {
    cancel_button->setText(z7::ui::runtime_support::L(402));
  }
  if (QPushButton* apply_button = button_box_->button(QDialogButtonBox::Apply)) {
    apply_button->setText(z7::ui::runtime_support::J(
        QStringLiteral("ui.options.buttons.apply")));
  }
  if (QPushButton* help_button = button_box_->button(QDialogButtonBox::Help)) {
    help_button->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(409)));
    help_button->setEnabled(false);
  }

  if (!windows_only_supported() && tabs_ != nullptr) {
    tabs_->setTabToolTip(0, win_only_tip);
    tabs_->setTabToolTip(1, finder_tip);
  } else if (tabs_ != nullptr) {
    tabs_->setTabToolTip(0, QString());
    tabs_->setTabToolTip(1, finder_supported ? QString() : finder_tip);
  }

}
}  // namespace z7::ui::filemanager
