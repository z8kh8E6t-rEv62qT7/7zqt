// src/ui/filemanager/src/main_window/view/view_runtime.cpp
// Role: UI translation and runtime settings application.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "main_window/drag_drop/drag_aware_views.h"

namespace z7::ui::filemanager {

namespace {

constexpr quint32 kToolbarMaskDefaultSentinel = quint32{1} << 31;
constexpr quint32 kToolbarMaskShowText = quint32{1} << 0;
constexpr quint32 kToolbarMaskLargeButtons = quint32{1} << 1;
constexpr quint32 kToolbarMaskStandardToolbar = quint32{1} << 2;
constexpr quint32 kToolbarMaskArchiveToolbar = quint32{1} << 3;
constexpr quint32 kToolbarMaskDefault =
    kToolbarMaskDefaultSentinel |
    kToolbarMaskShowText |
    kToolbarMaskStandardToolbar |
    kToolbarMaskArchiveToolbar;

struct ToolbarSettings {
  bool show_archive_toolbar = true;
  bool show_standard_toolbar = true;
  bool large_buttons = false;
  bool show_text = true;
};

QString toolbar_text(uint32_t lang_id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
}

ToolbarSettings decode_toolbar_settings(const QVariant& stored_value) {
  bool ok = false;
  const quint64 raw_mask = stored_value.toULongLong(&ok);
  const quint32 mask = ok ? static_cast<quint32>(raw_mask) : kToolbarMaskDefault;
  if ((mask & kToolbarMaskDefaultSentinel) != 0U) {
    return {};
  }

  ToolbarSettings settings;
  settings.show_text = (mask & kToolbarMaskShowText) != 0U;
  settings.large_buttons = (mask & kToolbarMaskLargeButtons) != 0U;
  settings.show_standard_toolbar = (mask & kToolbarMaskStandardToolbar) != 0U;
  settings.show_archive_toolbar = (mask & kToolbarMaskArchiveToolbar) != 0U;
  return settings;
}

quint32 encode_toolbar_settings(const ToolbarSettings& settings) {
  quint32 mask = 0;
  if (settings.show_text) {
    mask |= kToolbarMaskShowText;
  }
  if (settings.large_buttons) {
    mask |= kToolbarMaskLargeButtons;
  }
  if (settings.show_standard_toolbar) {
    mask |= kToolbarMaskStandardToolbar;
  }
  if (settings.show_archive_toolbar) {
    mask |= kToolbarMaskArchiveToolbar;
  }
  return mask;
}

}  // namespace

void MainWindow::apply_model_display_settings_to_panel(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return;
  }
  panel.model->set_show_dots(display_settings_.show_dots);
  panel.model->set_show_real_file_icons(display_settings_.show_real_file_icons);
  panel.model->set_timestamp_display(display_settings_.timestamp_level,
                                     display_settings_.timestamp_show_utc);
  update_status_for_panel(panel_index);
}

void MainWindow::apply_model_display_settings_to_all_panels() {
  for (int i = 0; i < 2; ++i) {
    apply_model_display_settings_to_panel(i);
  }
}

void MainWindow::apply_toolbar_action_texts() {
  compress_action_->setIconText(toolbar_text(7200));
  extract_action_->setIconText(toolbar_text(7201));
  test_action_->setIconText(toolbar_text(7202));
  copy_to_action_->setIconText(toolbar_text(7203));
  move_to_action_->setIconText(toolbar_text(7204));
  delete_action_->setIconText(toolbar_text(7205));
  properties_action_->setIconText(toolbar_text(7206));
}

void MainWindow::retranslate_ui() {
  update_window_title();

  file_menu_->setTitle(z7::ui::runtime_support::L(500));
  edit_menu_->setTitle(z7::ui::runtime_support::L(501));
  view_menu_->setTitle(z7::ui::runtime_support::L(502));
  favorites_menu_->setTitle(z7::ui::runtime_support::L(503));
  tools_menu_->setTitle(z7::ui::runtime_support::L(504));
  help_menu_->setTitle(z7::ui::runtime_support::L(505));

  open_action_->setText(z7::ui::runtime_support::L(540));
  open_inside_action_->setText(z7::ui::runtime_support::L(541));
  open_inside_one_action_->setText(z7::ui::runtime_support::L(541) + QStringLiteral(" *"));
  open_inside_parser_action_->setText(z7::ui::runtime_support::L(541) + QStringLiteral(" #"));
  open_outside_action_->setText(z7::ui::runtime_support::L(542));
  view_action_->setText(z7::ui::runtime_support::L(543));
  edit_action_->setText(z7::ui::runtime_support::L(544));
  rename_action_->setText(z7::ui::runtime_support::L(545));
  copy_to_action_->setText(z7::ui::runtime_support::L(546));
  move_to_action_->setText(z7::ui::runtime_support::L(547));
  delete_action_->setText(z7::ui::runtime_support::L(548));
  split_action_->setText(z7::ui::runtime_support::L(549));
  combine_action_->setText(z7::ui::runtime_support::L(550));
  properties_action_->setText(z7::ui::runtime_support::L(551));
  comment_action_->setText(z7::ui::runtime_support::L(552));
  crc_menu_->setTitle(z7::ui::runtime_support::J(
      QStringLiteral("ui.view.crc_menu_title")));
  diff_action_->setText(z7::ui::runtime_support::L(554));
  const QString version_control_suffix = QStringLiteral(" (Unsupported)");
  const QString version_control_tooltip =
      QStringLiteral("7-Zip version-control commands are not implemented in this Qt File Manager.");
  version_edit_action_->setText(
      QStringLiteral("Ver Edit (&1)") + version_control_suffix);
  version_commit_action_->setText(
      QStringLiteral("Ver Commit") + version_control_suffix);
  version_revert_action_->setText(
      QStringLiteral("Ver Revert") + version_control_suffix);
  version_diff_action_->setText(
      QStringLiteral("Ver Diff (&0)") + version_control_suffix);
  version_edit_action_->setToolTip(version_control_tooltip);
  version_commit_action_->setToolTip(version_control_tooltip);
  version_revert_action_->setToolTip(version_control_tooltip);
  version_diff_action_->setToolTip(version_control_tooltip);
  create_folder_action_->setText(z7::ui::runtime_support::L(555));
  create_file_action_->setText(z7::ui::runtime_support::L(556));
  exit_action_->setText(z7::ui::runtime_support::L(557));
  link_action_->setText(z7::ui::runtime_support::L(558));
  const auto alternate_streams_platform =
      z7::ui::runtime_support::PlatformSupport::kWindowsOnly;
  alternate_streams_action_->setText(
      z7::ui::runtime_support::with_platform_suffix_if_unsupported(
          z7::ui::runtime_support::L(559), alternate_streams_platform));
  alternate_streams_action_->setProperty(
      kActionCapabilityKeyProperty,
      action_capability_key(ActionCapabilityKey::kAlternateStreams));
  if (z7::ui::runtime_support::is_platform_supported(alternate_streams_platform)) {
    alternate_streams_action_->setProperty(kActionCapabilityReasonProperty, QVariant());
    alternate_streams_action_->setToolTip(QString());
  } else {
    alternate_streams_action_->setProperty(
        kActionCapabilityReasonProperty,
        z7::ui::runtime_support::platform_reason_key(alternate_streams_platform));
    alternate_streams_action_->setToolTip(
        z7::ui::runtime_support::platform_tooltip(alternate_streams_platform));
  }

  compress_action_->setText(toolbar_text(7200));
  extract_action_->setText(toolbar_text(7201));
  test_action_->setText(toolbar_text(7202));
  apply_toolbar_action_texts();

  select_all_action_->setText(z7::ui::runtime_support::L(600));
  deselect_all_action_->setText(z7::ui::runtime_support::L(601));
  invert_selection_action_->setText(z7::ui::runtime_support::L(602));
  select_action_->setText(z7::ui::runtime_support::L(603));
  deselect_action_->setText(z7::ui::runtime_support::L(604));
  select_by_type_action_->setText(z7::ui::runtime_support::L(605));
  deselect_by_type_action_->setText(z7::ui::runtime_support::L(606));

  large_icons_action_->setText(z7::ui::runtime_support::L(700));
  small_icons_action_->setText(z7::ui::runtime_support::L(701));
  list_mode_action_->setText(z7::ui::runtime_support::L(702));
  details_mode_action_->setText(z7::ui::runtime_support::L(703));
  sort_name_action_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)));
  sort_type_action_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1020)));
  sort_date_action_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1012)));
  sort_size_action_->setText(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1007)));
  unsorted_action_->setText(z7::ui::runtime_support::L(730));
  flat_view_action_->setText(z7::ui::runtime_support::L(731));
  two_panels_action_->setText(z7::ui::runtime_support::L(732));
  if (toolbars_submenu_ != nullptr) {
    toolbars_submenu_->setTitle(z7::ui::runtime_support::L(733));
  }
  archive_toolbar_action_->setText(z7::ui::runtime_support::L(750));
  standard_toolbar_action_->setText(z7::ui::runtime_support::L(751));
  large_buttons_action_->setText(z7::ui::runtime_support::L(752));
  show_buttons_text_action_->setText(z7::ui::runtime_support::L(753));
  open_root_action_->setText(z7::ui::runtime_support::L(734));
  open_parent_action_->setText(z7::ui::runtime_support::L(735));
  if (up_dir_button_ != nullptr) {
    up_dir_button_->setToolTip(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(735)));
  }
  folders_history_action_->setText(z7::ui::runtime_support::L(736));
  refresh_action_->setText(z7::ui::runtime_support::L(737));
  auto_refresh_action_->setText(z7::ui::runtime_support::L(738));
  time_utc_action_->setText(z7::ui::runtime_support::J(
      QStringLiteral("ui.view.time_utc")));

  if (add_to_favorites_menu_ != nullptr) {
    add_to_favorites_menu_->setTitle(z7::ui::runtime_support::L(800));
  }

  options_action_->setText(z7::ui::runtime_support::L(900));
  benchmark_action_->setText(z7::ui::runtime_support::L(901));
  benchmark2_action_->setText(
      QStringLiteral("%1 2").arg(lang_or(901)));
  temp_files_action_->setText(z7::ui::runtime_support::L(910));

  contents_action_->setText(z7::ui::runtime_support::L(960));
  about_action_->setText(z7::ui::runtime_support::L(961));

  archive_toolbar_->setWindowTitle(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(750)));
  standard_toolbar_->setWindowTitle(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(751)));

  for (int i = 0; i < 2; ++i) {
    if (panels_[i].model != nullptr) {
      panels_[i].model->notify_language_changed();
    }
    update_status_for_panel(i);
  }
  refresh_all_details_column_visibility();
  update_time_menu();
  update_view_menu_checks();
  sync_path_bar_from_current_dir();
  update_status();
}

void MainWindow::load_runtime_settings() {
  z7::platform::qt::PortableSettings settings;
  confirm_delete_ = settings.value(QString::fromLatin1(kSettingsFmConfirmDelete), true).toBool();
  display_settings_ = load_display_settings(settings);

  const ToolbarSettings toolbar_settings =
      decode_toolbar_settings(settings.value(QString::fromLatin1(kSettingsFmToolbars),
                                             kToolbarMaskDefault));
  const bool auto_refresh =
      settings.value(QString::fromLatin1(kSettingsFmAutoRefresh), true).toBool();
  two_panels_visible_ = read_fm_panels_state(settings).two_panels;

  archive_toolbar_action_->setChecked(toolbar_settings.show_archive_toolbar);
  standard_toolbar_action_->setChecked(toolbar_settings.show_standard_toolbar);
  large_buttons_action_->setChecked(toolbar_settings.large_buttons);
  show_buttons_text_action_->setChecked(toolbar_settings.show_text);
  auto_refresh_action_->setChecked(auto_refresh);
  two_panels_action_->setChecked(two_panels_visible_);
  if (time_utc_action_ != nullptr) {
    time_utc_action_->setChecked(display_settings_.timestamp_show_utc);
  }
}

void MainWindow::apply_runtime_settings() {
  const bool show_archive_toolbar =
      archive_toolbar_action_ != nullptr && archive_toolbar_action_->isChecked();
  const bool show_standard_toolbar =
      standard_toolbar_action_ != nullptr && standard_toolbar_action_->isChecked();
  const bool large_buttons =
      large_buttons_action_ != nullptr && large_buttons_action_->isChecked();
  const bool show_text =
      show_buttons_text_action_ == nullptr || show_buttons_text_action_->isChecked();

  archive_toolbar_->setVisible(show_archive_toolbar);
  standard_toolbar_->setVisible(show_standard_toolbar);
  const int toolbar_icon_size = z7::platform::qt::toolbar_icon_extent(large_buttons, this);
  const QSize icon_size(toolbar_icon_size, toolbar_icon_size);
  archive_toolbar_->setIconSize(icon_size);
  standard_toolbar_->setIconSize(icon_size);
  const Qt::ToolButtonStyle button_style =
      show_text ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly;
  archive_toolbar_->setToolButtonStyle(button_style);
  standard_toolbar_->setToolButtonStyle(button_style);
  apply_toolbar_action_texts();

  const QAbstractItemView::SelectionMode selection_mode =
      display_settings_.alternative_selection_mode ? QAbstractItemView::SingleSelection
                                                   : QAbstractItemView::ExtendedSelection;
  QVector<int> panels_to_reload_archive_view;

  for (int i = 0; i < 2; ++i) {
    PanelController& panel = panels_[i];
    bool should_reload_archive_view = false;
    if (panel.model != nullptr) {
      should_reload_archive_view =
          panel.model->show_dots() != display_settings_.show_dots &&
          in_archive_view_for_panel(i);
      apply_model_display_settings_to_panel(i);
    }
    if (panel.ui.details_view != nullptr) {
      // Primary-column-owned selection means we keep SelectItems at the view
      // layer; only the user-facing "alternative selection mode" still toggles
      // single/extended selection.
      panel.ui.details_view->setSelectionMode(selection_mode);
      // "Full row select" is now expressed as a faint row-wide hover fill;
      // "grid lines" toggle the bottom border painted by the delegate. Both
      // live in the StructuredListConfig rather than a global stylesheet.
      const QVector<int> current_widths =
          column_width_persistence::capture_widths(
              panel.ui.details_view->horizontalHeader(),
              DirectoryListModel::kColumnCount);
      auto config = panel.ui.details_view->config();
      config.style.row_hover_bg =
          display_settings_.full_row_select
              ? QColor(0, 0, 0, 14)
              : QColor();
      config.style.grid_line =
          display_settings_.show_grid_lines
              ? QColor(0, 0, 0, 22)
              : QColor();
      panel.ui.details_view->set_config(config);
      if (current_widths.size() == DirectoryListModel::kColumnCount) {
        column_width_persistence::apply_widths(
            panel.ui.details_view->horizontalHeader(),
            current_widths);
      }
      panel.ui.details_view->viewport()->update();
    }
    if (panel.ui.icon_list_view != nullptr) {
      panel.ui.icon_list_view->setSelectionMode(selection_mode);
    }
    if (should_reload_archive_view) {
      panels_to_reload_archive_view.push_back(i);
    }
    rebind_auto_refresh_watcher_for_panel(i);
  }

  if (panels_[1].ui.container != nullptr) {
    panels_[1].ui.container->setVisible(two_panels_visible_);
  }
  if (panels_splitter_ != nullptr) {
    if (two_panels_visible_) {
      const QList<int> current_sizes = panels_splitter_->sizes();
      const bool has_two_visible_sizes =
          current_sizes.size() >= 2 &&
          current_sizes.at(0) > 0 &&
          current_sizes.at(1) > 0;
      if (!has_two_visible_sizes) {
        panels_splitter_->setSizes({1, 1});
      }
    } else {
      panels_splitter_->setSizes({1, 0});
      if (active_panel_index_ == 1) {
        set_active_panel(0);
      }
    }
  }

  update_time_menu();
  update_view_menu_checks();

  if (auto_refresh_action_->isChecked()) {
    auto_refresh_timer_->start();
  } else {
    auto_refresh_timer_->stop();
  }

  z7::platform::qt::PortableSettings settings;
  ToolbarSettings toolbar_settings;
  toolbar_settings.show_archive_toolbar = show_archive_toolbar;
  toolbar_settings.show_standard_toolbar = show_standard_toolbar;
  toolbar_settings.large_buttons = large_buttons;
  toolbar_settings.show_text = show_text;
  settings.setValue(QString::fromLatin1(kSettingsFmToolbars),
                    encode_toolbar_settings(toolbar_settings));
  settings.setValue(QString::fromLatin1(kSettingsFmAutoRefresh),
                    auto_refresh_action_->isChecked());
  save_display_settings(settings, display_settings_);

  refresh_action_states();
  update_status();
  reload_archive_virtual_directories_serially(
      std::move(panels_to_reload_archive_view));
}


}  // namespace z7::ui::filemanager
