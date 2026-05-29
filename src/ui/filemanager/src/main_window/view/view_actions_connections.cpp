// src/ui/filemanager/src/main_window/view/view_actions_connections.cpp
// Role: Signal-slot wiring for MainWindow actions and interactive controls.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

void MainWindow::setup_connections() {
  connect(file_menu_, &QMenu::aboutToShow, this, [this]() {
    refresh_action_states();
    rebuild_file_menu_seven_zip_section();
  });
  connect(crc_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuild_file_crc_menu);
  connect(favorites_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuild_favorites_menu);
  connect(view_menu_, &QMenu::aboutToShow, this, [this]() {
    update_time_menu();
    update_view_menu_checks();
    refresh_action_states();
  });

  connect(open_action_, &QAction::triggered, this, &MainWindow::on_open_requested);
  connect(open_inside_action_, &QAction::triggered, this, &MainWindow::on_open_inside_requested);
  connect(open_inside_one_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_open_inside_one_requested);
  connect(open_inside_parser_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_open_inside_parser_requested);
  connect(open_outside_action_, &QAction::triggered, this, &MainWindow::on_open_outside_requested);
  connect(view_action_, &QAction::triggered, this, &MainWindow::on_view_requested);
  connect(edit_action_, &QAction::triggered, this, &MainWindow::on_edit_requested);
  connect(rename_action_, &QAction::triggered, this, &MainWindow::on_rename_requested);
  connect(copy_to_action_, &QAction::triggered, this, &MainWindow::on_copy_to_requested);
  connect(move_to_action_, &QAction::triggered, this, &MainWindow::on_move_to_requested);
  connect(delete_action_, &QAction::triggered, this, &MainWindow::on_delete_requested);
  connect(split_action_, &QAction::triggered, this, &MainWindow::on_split_requested);
  connect(combine_action_, &QAction::triggered, this, &MainWindow::on_combine_requested);
  connect(diff_action_, &QAction::triggered, this, &MainWindow::on_diff_requested);
  connect(create_folder_action_, &QAction::triggered, this, &MainWindow::on_create_folder_requested);
  connect(create_file_action_, &QAction::triggered, this, &MainWindow::on_create_file_requested);

  connect(compress_action_, &QAction::triggered, this, &MainWindow::on_compress_requested);
  connect(extract_action_, &QAction::triggered, this, &MainWindow::on_extract_requested);
  connect(test_action_, &QAction::triggered, this, &MainWindow::on_test_requested);
  connect(properties_action_, &QAction::triggered, this, &MainWindow::show_properties_dialog);
  connect(open_parent_action_, &QAction::triggered, this, &MainWindow::on_open_parent_requested);
  connect(open_root_action_, &QAction::triggered, this, &MainWindow::on_open_root_requested);
  connect(folders_history_action_, &QAction::triggered, this, &MainWindow::on_folders_history_requested);
  connect(refresh_action_, &QAction::triggered, this, &MainWindow::on_refresh_requested);
  connect(options_action_, &QAction::triggered, this, &MainWindow::on_options_requested);
  connect(benchmark_action_, &QAction::triggered, this, &MainWindow::on_benchmark_requested);
  connect(benchmark2_action_, &QAction::triggered, this, &MainWindow::on_benchmark2_requested);
  connect(temp_files_action_, &QAction::triggered, this, &MainWindow::on_temp_files_requested);
  connect(contents_action_, &QAction::triggered, this, &MainWindow::on_contents_requested);
  connect(comment_action_, &QAction::triggered, this, &MainWindow::on_comment_requested);
  connect(link_action_, &QAction::triggered, this, &MainWindow::on_link_requested);
  connect(alternate_streams_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_alternate_streams_requested);
  connect(exit_action_, &QAction::triggered, this, &QWidget::close);
  connect(about_action_, &QAction::triggered, this, [this]() {
    QMessageBox::about(
        this,
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(961)),
        z7::ui::runtime_support::J(QStringLiteral("ui.about.qt_edition")));
  });

  connect(select_all_action_, &QAction::triggered, this, [this]() {
    set_operable_rows_selected_for_panel(active_panel_index_, true);
  });
  connect(deselect_all_action_, &QAction::triggered, this, [this]() {
    set_operable_rows_selected_for_panel(active_panel_index_, false);
  });
  connect(invert_selection_action_, &QAction::triggered, this, [this]() {
    invert_operable_selection_for_panel(active_panel_index_);
  });
  connect(select_action_, &QAction::triggered, this, &MainWindow::on_select_requested);
  connect(deselect_action_, &QAction::triggered, this, &MainWindow::on_deselect_requested);
  connect(select_by_type_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_select_by_type_requested);
  connect(deselect_by_type_action_,
          &QAction::triggered,
          this,
          &MainWindow::on_deselect_by_type_requested);

  connect(large_icons_action_, &QAction::triggered, this, [this]() {
    on_view_mode_action_triggered(PanelController::kViewModeLargeIcons);
  });
  connect(small_icons_action_, &QAction::triggered, this, [this]() {
    on_view_mode_action_triggered(PanelController::kViewModeSmallIcons);
  });
  connect(list_mode_action_, &QAction::triggered, this, [this]() {
    on_view_mode_action_triggered(PanelController::kViewModeList);
  });
  connect(details_mode_action_, &QAction::triggered, this, [this]() {
    on_view_mode_action_triggered(PanelController::kViewModeDetails);
  });
  connect(sort_name_action_, &QAction::triggered, this, [this]() {
    on_sort_mode_action_triggered(kSortActionName);
  });
  connect(sort_type_action_, &QAction::triggered, this, [this]() {
    on_sort_mode_action_triggered(kSortActionType);
  });
  connect(sort_date_action_, &QAction::triggered, this, [this]() {
    on_sort_mode_action_triggered(kSortActionDate);
  });
  connect(sort_size_action_, &QAction::triggered, this, [this]() {
    on_sort_mode_action_triggered(kSortActionSize);
  });
  connect(unsorted_action_, &QAction::triggered, this, [this]() {
    on_sort_mode_action_triggered(kSortActionUnsorted);
  });
  connect(flat_view_action_, &QAction::triggered, this, &MainWindow::on_flat_view_action_triggered);
  connect(two_panels_action_, &QAction::triggered, this, &MainWindow::on_two_panels_action_triggered);
  connect(time_day_action_, &QAction::triggered, this, [this]() {
    on_time_precision_requested(DirectoryListModel::kTimestampPrintLevelDay);
  });
  connect(time_min_action_, &QAction::triggered, this, [this]() {
    on_time_precision_requested(DirectoryListModel::kTimestampPrintLevelMin);
  });
  connect(time_sec_action_, &QAction::triggered, this, [this]() {
    on_time_precision_requested(DirectoryListModel::kTimestampPrintLevelSec);
  });
  connect(time_ntfs_action_, &QAction::triggered, this, [this]() {
    on_time_precision_requested(DirectoryListModel::kTimestampPrintLevelNtfs);
  });
  connect(time_ns_action_, &QAction::triggered, this, [this]() {
    on_time_precision_requested(DirectoryListModel::kTimestampPrintLevelNs);
  });
  connect(time_utc_action_, &QAction::triggered, this, &MainWindow::on_toggle_time_utc);

  connect(archive_toolbar_action_, &QAction::triggered, this, &MainWindow::apply_runtime_settings);
  connect(standard_toolbar_action_, &QAction::triggered, this, &MainWindow::apply_runtime_settings);
  connect(large_buttons_action_, &QAction::triggered, this, &MainWindow::apply_runtime_settings);
  connect(show_buttons_text_action_, &QAction::triggered, this, &MainWindow::apply_runtime_settings);
  connect(auto_refresh_action_, &QAction::triggered, this, [this]() {
    apply_runtime_settings();
  });
  connect(auto_refresh_timer_, &QTimer::timeout, this, &MainWindow::on_auto_refresh_timer_tick);

  connect(up_dir_button_, &QToolButton::clicked, this, &MainWindow::on_open_parent_requested);

  connect(path_combo_->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
    if (!navigate_to_path_from_bar(path_combo_->currentText())) {
      sync_path_bar_from_current_dir();
    }
  });

  connect(path_combo_,
          QOverload<int>::of(&QComboBox::activated),
          this,
          [this](int index) {
            const QString data =
                path_combo_->itemData(index, Qt::UserRole).toString();
            if (data.isEmpty()) {
              sync_path_bar_from_current_dir();
              return;
            }

            if (!navigate_to_path_from_bar(data)) {
              sync_path_bar_from_current_dir();
            }
          });

  connect(path_combo_->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
    sync_path_bar_from_current_dir();
  });
}

}  // namespace z7::ui::filemanager
