// src/ui/filemanager/src/main_window/view/view_actions_setup.cpp
// Role: Menu and action object creation for MainWindow.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

void MainWindow::setup_actions() {
  menuBar()->setNativeMenuBar(false);

  file_menu_ = menuBar()->addMenu(QString());
  edit_menu_ = menuBar()->addMenu(QString());
  view_menu_ = menuBar()->addMenu(QString());
  favorites_menu_ = menuBar()->addMenu(QString());
  tools_menu_ = menuBar()->addMenu(QString());
  help_menu_ = menuBar()->addMenu(QString());

  open_action_ = new QAction(this);
  open_inside_action_ = new QAction(this);
  open_inside_one_action_ = new QAction(this);
  open_inside_parser_action_ = new QAction(this);
  open_outside_action_ = new QAction(this);
  view_action_ = new QAction(this);
  edit_action_ = new QAction(this);
  rename_action_ = new QAction(this);
  copy_to_action_ = new QAction(this);
  move_to_action_ = new QAction(this);
  delete_action_ = new QAction(this);
  split_action_ = new QAction(this);
  combine_action_ = new QAction(this);
  properties_action_ = new QAction(this);
  comment_action_ = new QAction(this);
  diff_action_ = new QAction(this);
  version_edit_action_ = new QAction(this);
  version_commit_action_ = new QAction(this);
  version_revert_action_ = new QAction(this);
  version_diff_action_ = new QAction(this);
  create_folder_action_ = new QAction(this);
  create_file_action_ = new QAction(this);
  exit_action_ = new QAction(this);
  link_action_ = new QAction(this);
  alternate_streams_action_ = new QAction(this);

  compress_action_ = new QAction(this);
  extract_action_ = new QAction(this);
  test_action_ = new QAction(this);

  select_all_action_ = new QAction(this);
  deselect_all_action_ = new QAction(this);
  invert_selection_action_ = new QAction(this);
  select_action_ = new QAction(this);
  deselect_action_ = new QAction(this);
  select_by_type_action_ = new QAction(this);
  deselect_by_type_action_ = new QAction(this);

  large_icons_action_ = new QAction(this);
  small_icons_action_ = new QAction(this);
  list_mode_action_ = new QAction(this);
  details_mode_action_ = new QAction(this);
  sort_name_action_ = new QAction(this);
  sort_type_action_ = new QAction(this);
  sort_date_action_ = new QAction(this);
  sort_size_action_ = new QAction(this);
  unsorted_action_ = new QAction(this);
  flat_view_action_ = new QAction(this);
  two_panels_action_ = new QAction(this);
  open_root_action_ = new QAction(this);
  open_parent_action_ = new QAction(this);
  folders_history_action_ = new QAction(this);
  refresh_action_ = new QAction(this);
  auto_refresh_action_ = new QAction(this);
  archive_toolbar_action_ = new QAction(this);
  standard_toolbar_action_ = new QAction(this);
  large_buttons_action_ = new QAction(this);
  show_buttons_text_action_ = new QAction(this);
  time_day_action_ = new QAction(this);
  time_min_action_ = new QAction(this);
  time_sec_action_ = new QAction(this);
  time_ntfs_action_ = new QAction(this);
  time_ns_action_ = new QAction(this);
  time_utc_action_ = new QAction(this);
  add_to_favorites_menu_ = new QMenu(this);
  view_mode_action_group_ = new QActionGroup(this);
  sort_action_group_ = new QActionGroup(this);
  time_action_group_ = new QActionGroup(this);

  options_action_ = new QAction(this);
  benchmark_action_ = new QAction(this);
  benchmark2_action_ = new QAction(this);
  temp_files_action_ = new QAction(this);
  benchmark2_action_->setVisible(false);

  contents_action_ = new QAction(this);
  contents_action_->setEnabled(false);
  about_action_ = new QAction(this);

  open_action_->setShortcut(QKeySequence(Qt::Key_Return));
  open_inside_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+PgDown")));
  open_outside_action_->setShortcut(QKeySequence(QStringLiteral("Shift+Return")));
  view_action_->setShortcut(QKeySequence(Qt::Key_F3));
  edit_action_->setShortcut(QKeySequence(Qt::Key_F4));
  contents_action_->setShortcut(QKeySequence(Qt::Key_F1));
  rename_action_->setShortcut(QKeySequence(Qt::Key_F2));
  copy_to_action_->setShortcut(QKeySequence(Qt::Key_F5));
  move_to_action_->setShortcut(QKeySequence(Qt::Key_F6));
  delete_action_->setShortcuts({
      QKeySequence(Qt::Key_Delete),
      QKeySequence(Qt::SHIFT | Qt::Key_Delete),
  });
  properties_action_->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
  comment_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
  create_folder_action_->setShortcut(QKeySequence(Qt::Key_F7));
  create_file_action_->setShortcuts({
      QKeySequence(QStringLiteral("Ctrl+N")),
      QKeySequence(Qt::SHIFT | Qt::Key_F4),
  });
  large_icons_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
  small_icons_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
  list_mode_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
  details_mode_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
  sort_name_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F3")));
  sort_type_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F4")));
  sort_date_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
  sort_size_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F6")));
  unsorted_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F7")));
  two_panels_action_->setShortcut(QKeySequence(Qt::Key_F9));
  open_root_action_->setShortcut(QKeySequence(QStringLiteral("\\")));
  open_parent_action_->setShortcut(QKeySequence(Qt::Key_Backspace));
  folders_history_action_->setShortcut(QKeySequence(QStringLiteral("Alt+F12")));
  refresh_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  options_action_->setShortcut(QKeySequence(QStringLiteral("Alt+O")));

  compress_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Add2.bmp")));
  extract_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Extract2.bmp")));
  test_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Test2.bmp")));
  copy_to_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Copy2.bmp")));
  move_to_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Move2.bmp")));
  delete_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Delete2.bmp")));
  properties_action_->setIcon(load_masked_toolbar_icon(QStringLiteral(":/z7/fm-icons/Info2.bmp")));

  view_mode_action_group_->setExclusive(true);
  sort_action_group_->setExclusive(true);
  time_action_group_->setExclusive(true);
  for (QAction* action : {large_icons_action_, small_icons_action_, list_mode_action_, details_mode_action_}) {
    action->setCheckable(true);
    view_mode_action_group_->addAction(action);
  }
  for (QAction* action : {sort_name_action_, sort_type_action_, sort_date_action_, sort_size_action_, unsorted_action_}) {
    action->setCheckable(true);
    sort_action_group_->addAction(action);
  }
  for (QAction* action : {time_day_action_, time_min_action_, time_sec_action_, time_ntfs_action_, time_ns_action_}) {
    action->setCheckable(true);
    time_action_group_->addAction(action);
  }
  flat_view_action_->setCheckable(true);
  two_panels_action_->setCheckable(true);
  auto_refresh_action_->setCheckable(true);
  archive_toolbar_action_->setCheckable(true);
  standard_toolbar_action_->setCheckable(true);
  large_buttons_action_->setCheckable(true);
  show_buttons_text_action_->setCheckable(true);
  time_utc_action_->setCheckable(true);

  file_menu_->addAction(open_action_);
  file_menu_->addAction(open_inside_action_);
  file_menu_->addAction(open_inside_one_action_);
  file_menu_->addAction(open_inside_parser_action_);
  file_menu_->addAction(open_outside_action_);
  file_menu_->addAction(view_action_);
  file_menu_->addAction(edit_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(rename_action_);
  file_menu_->addAction(copy_to_action_);
  file_menu_->addAction(move_to_action_);
  file_menu_->addAction(delete_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(split_action_);
  file_menu_->addAction(combine_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(properties_action_);
  file_menu_->addAction(comment_action_);
  crc_menu_ = file_menu_->addMenu(QString());
  rebuild_file_crc_menu();
  file_menu_->addAction(diff_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(create_folder_action_);
  file_menu_->addAction(create_file_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(link_action_);
  file_menu_->addAction(alternate_streams_action_);
  file_menu_->addSeparator();
  file_menu_->addAction(exit_action_);
  file_menu_->addAction(version_edit_action_);
  file_menu_->addAction(version_commit_action_);
  file_menu_->addAction(version_revert_action_);
  file_menu_->addAction(version_diff_action_);

  edit_menu_->addAction(select_all_action_);
  edit_menu_->addAction(deselect_all_action_);
  edit_menu_->addAction(invert_selection_action_);
  edit_menu_->addAction(select_action_);
  edit_menu_->addAction(deselect_action_);
  edit_menu_->addSeparator();
  edit_menu_->addAction(select_by_type_action_);
  edit_menu_->addAction(deselect_by_type_action_);

  view_menu_->addAction(large_icons_action_);
  view_menu_->addAction(small_icons_action_);
  view_menu_->addAction(list_mode_action_);
  view_menu_->addAction(details_mode_action_);
  view_menu_->addSeparator();
  view_menu_->addAction(sort_name_action_);
  view_menu_->addAction(sort_type_action_);
  view_menu_->addAction(sort_date_action_);
  view_menu_->addAction(sort_size_action_);
  view_menu_->addAction(unsorted_action_);
  view_menu_->addSeparator();
  view_menu_->addAction(flat_view_action_);
  view_menu_->addAction(two_panels_action_);

  time_submenu_ = view_menu_->addMenu(QString());
  time_submenu_->addAction(time_day_action_);
  time_submenu_->addAction(time_min_action_);
  time_submenu_->addAction(time_sec_action_);
  time_submenu_->addAction(time_ntfs_action_);
  time_submenu_->addAction(time_ns_action_);
  time_submenu_->addSeparator();
  time_submenu_->addAction(time_utc_action_);

  toolbars_submenu_ = view_menu_->addMenu(QString());
  toolbars_submenu_->addAction(archive_toolbar_action_);
  toolbars_submenu_->addAction(standard_toolbar_action_);
  toolbars_submenu_->addSeparator();
  toolbars_submenu_->addAction(large_buttons_action_);
  toolbars_submenu_->addAction(show_buttons_text_action_);

  view_menu_->addAction(open_root_action_);
  view_menu_->addAction(open_parent_action_);
  view_menu_->addAction(folders_history_action_);
  view_menu_->addAction(refresh_action_);
  view_menu_->addAction(auto_refresh_action_);

  favorites_menu_->addMenu(add_to_favorites_menu_);

  tools_menu_->addAction(options_action_);
  tools_menu_->addSeparator();
  tools_menu_->addAction(benchmark_action_);
  tools_menu_->addAction(benchmark2_action_);
  tools_menu_->addSeparator();
  tools_menu_->addAction(temp_files_action_);

  help_menu_->addAction(contents_action_);
  help_menu_->addSeparator();
  help_menu_->addAction(about_action_);

  archive_toolbar_ = addToolBar(QString());
  archive_toolbar_->setMovable(false);
  archive_toolbar_->setIconSize(
      QSize(z7::platform::qt::toolbar_icon_extent(false, this),
            z7::platform::qt::toolbar_icon_extent(false, this)));
  archive_toolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  archive_toolbar_->addAction(compress_action_);
  archive_toolbar_->addAction(extract_action_);
  archive_toolbar_->addAction(test_action_);

  standard_toolbar_ = addToolBar(QString());
  standard_toolbar_->setMovable(false);
  standard_toolbar_->setIconSize(
      QSize(z7::platform::qt::toolbar_icon_extent(false, this),
            z7::platform::qt::toolbar_icon_extent(false, this)));
  standard_toolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  standard_toolbar_->addAction(copy_to_action_);
  standard_toolbar_->addAction(move_to_action_);
  standard_toolbar_->addAction(delete_action_);
  standard_toolbar_->addAction(properties_action_);

  details_mode_action_->setChecked(true);
  sort_name_action_->setChecked(true);
  unsorted_action_->setChecked(false);
  flat_view_action_->setChecked(false);
  two_panels_action_->setChecked(false);
  archive_toolbar_action_->setChecked(true);
  standard_toolbar_action_->setChecked(true);
  large_buttons_action_->setChecked(false);
  show_buttons_text_action_->setChecked(true);
  time_min_action_->setChecked(true);
  time_utc_action_->setChecked(false);
}

}  // namespace z7::ui::filemanager
