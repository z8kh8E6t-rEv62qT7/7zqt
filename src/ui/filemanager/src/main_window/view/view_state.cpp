// src/ui/filemanager/src/main_window/view/view_state.cpp
// Role: Status refresh and selection/query helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

namespace {

QString format_original_status_size(quint64 value) {
  const QString digits = QString::number(value);
  if (digits.size() <= 3) {
    return digits;
  }

  QString out;
  out.reserve(digits.size() + digits.size() / 3);
  const int first_group = (digits.size() % 3 == 0) ? 3 : (digits.size() % 3);
  out += digits.left(first_group);
  for (int i = first_group; i < digits.size(); i += 3) {
    out += QLatin1Char(' ');
    out += digits.mid(i, 3);
  }
  return out;
}

quint64 model_row_size(const DirectoryListModel& model, int row) {
  const QVariant size_key =
      model.data(model.index(row, DirectoryListModel::kSizeColumn),
                 z7::ui::widgets::StructuredListSortFilterProxy::kSortKeyRole);
  bool ok = false;
  const quint64 size = size_key.toULongLong(&ok);
  return ok ? size : 0;
}

int non_parent_row_count(const DirectoryListModel& model) {
  int count = 0;
  const int rows = model.rowCount();
  for (int row = 0; row < rows; ++row) {
    if (!model.is_parent_link_for_row(row)) {
      ++count;
    }
  }
  return count;
}

}  // namespace

void MainWindow::refresh_active_panel_chrome() {
  sync_path_bar_from_current_dir();
  update_window_title();
  update_view_menu_checks();
  update_status();
}

void MainWindow::update_window_title() {
  if (in_archive_view()) {
    setWindowTitle(archive_virtual_display_path_for_panel(active_panel_index_));
    return;
  }

  setWindowTitle(QDir::toNativeSeparators(current_directory()));
}

void MainWindow::show_context_menu(const QPoint& pos) {
  auto* source_view = qobject_cast<QAbstractItemView*>(sender());
  if (source_view != nullptr) {
    const int panel_index = panel_index_for_view(source_view);
    if (panel_index >= 0) {
      set_active_panel(panel_index);
    }
  }
  // Right-click never mutates selection. Callers rely on the current selection
  // as-is; no implicit "select row under pointer".
  refresh_action_states();
  const bool shift_pressed =
      (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
  const SevenZipMenuState seven_zip_state =
      compute_seven_zip_menu_state(shift_pressed);

  QMenu menu(this);
  populate_context_menu(&menu, seven_zip_state);
  QAbstractItemView* view = source_view != nullptr ? source_view : active_item_view();
  if (view == nullptr) {
    return;
  }
  menu.exec(view->viewport()->mapToGlobal(pos));
}

void MainWindow::populate_context_menu(QMenu* menu, const SevenZipMenuState& seven_zip_state) {
  if (menu == nullptr) {
    return;
  }

  append_seven_zip_submenu(menu, seven_zip_state);

  menu->addAction(open_action_);
  menu->addAction(open_inside_action_);
  menu->addAction(open_inside_one_action_);
  menu->addAction(open_inside_parser_action_);
  menu->addAction(open_outside_action_);
  menu->addAction(view_action_);
  menu->addAction(edit_action_);
  menu->addSeparator();
  menu->addAction(rename_action_);
  menu->addAction(copy_to_action_);
  menu->addAction(move_to_action_);
  menu->addAction(delete_action_);
  menu->addSeparator();
  menu->addAction(split_action_);
  menu->addAction(combine_action_);
  menu->addSeparator();
  menu->addAction(properties_action_);
  menu->addAction(comment_action_);

  QMenu* crc_sub = menu->addMenu(z7::ui::runtime_support::J(
      QStringLiteral("ui.view.crc_menu_title")));
  populate_crc_hash_menu(
      crc_sub,
      has_oper_smart_real_items(),
      [this](const QString& method) { on_hash_with_method_requested(method); });

  menu->addAction(diff_action_);
  menu->addSeparator();
  menu->addAction(create_folder_action_);
  menu->addAction(create_file_action_);
  menu->addSeparator();
  menu->addAction(link_action_);
  menu->addAction(alternate_streams_action_);
  QAction* version_separator = menu->addSeparator();
  version_separator->setVisible(version_edit_action_->isVisible() ||
                                version_commit_action_->isVisible() ||
                                version_revert_action_->isVisible() ||
                                version_diff_action_->isVisible());
  menu->addAction(version_edit_action_);
  menu->addAction(version_commit_action_);
  menu->addAction(version_revert_action_);
  menu->addAction(version_diff_action_);
}

void MainWindow::update_status() {
  update_status_for_panel(active_panel_index_);
  refresh_action_states();
}

void MainWindow::update_status_for_panel(int panel_index) {
  if (panel_index < 0 || panel_index > 1) {
    return;
  }

  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.ui.status_selected_count == nullptr ||
      panel.ui.status_selected_size == nullptr ||
      panel.ui.status_focused_size == nullptr ||
      panel.ui.status_focused_modified == nullptr) {
    return;
  }

  const QModelIndexList selected_rows = panel.selected_rows_including_parent_link();
  const bool has_selected_list_row = !selected_rows.isEmpty();
  int selected_count = 0;
  quint64 selected_size = 0;
  for (const QModelIndex& row : selected_rows) {
    if (!row.isValid() || panel.model->is_parent_link_for_row(row.row())) {
      continue;
    }
    ++selected_count;
    selected_size += model_row_size(*panel.model, row.row());
  }

  panel.ui.status_selected_count->setText(
      z7::ui::runtime_support::LF(
          3002,
          {QStringLiteral("%1 / %2")
               .arg(QString::number(selected_count),
                    QString::number(non_parent_row_count(*panel.model)))}));
  panel.ui.status_selected_size->setText(
      selected_count > 0 ? format_original_status_size(selected_size)
                         : QString());

  QString focused_size;
  QString focused_modified;
  if (has_selected_list_row) {
    const QAbstractItemView* view = panel.current_item_view();
    QModelIndex focused_proxy =
        view != nullptr ? view->currentIndex() : QModelIndex();
    if (!focused_proxy.isValid() && panel.selection_model() != nullptr) {
      focused_proxy = panel.selection_model()->currentIndex();
    }
    const QModelIndex focused_source = panel.map_proxy_to_source(focused_proxy);
    if (focused_source.isValid() &&
        !panel.model->is_parent_link_for_row(focused_source.row())) {
      focused_modified =
          panel.model->data(
              panel.model->index(focused_source.row(), DirectoryListModel::kModifiedColumn),
              Qt::DisplayRole)
              .toString();
      focused_size =
          format_original_status_size(model_row_size(*panel.model,
                                                     focused_source.row()));
    }
  }
  panel.ui.status_focused_size->setText(focused_size);
  panel.ui.status_focused_modified->setText(focused_modified);
}

void MainWindow::clear_transient_status_messages() {
  for (PanelController& panel : panels_) {
    if (panel.ui.status_transient_message != nullptr) {
      panel.ui.status_transient_message->clear();
    }
  }
}

void MainWindow::show_transient_status_message(const QString& message,
                                               int timeout_ms,
                                               int panel_index) {
  ++transient_status_generation_;
  clear_transient_status_messages();

  int target_panel = panel_index;
  if (target_panel < 0 || target_panel > 1) {
    target_panel = active_panel_index_;
  }
  if (target_panel < 0 || target_panel > 1) {
    return;
  }

  QLabel* label = panel_controller(target_panel).ui.status_transient_message;
  if (label == nullptr) {
    return;
  }
  label->setText(message);
  if (timeout_ms <= 0 || message.isEmpty()) {
    return;
  }

  const quint64 generation = transient_status_generation_;
  QTimer::singleShot(timeout_ms, this, [this, generation]() {
    if (generation != transient_status_generation_) {
      return;
    }
    clear_transient_status_messages();
  });
}

bool MainWindow::refresh_directory_for_panel(int panel_index) {
  if (in_archive_view_for_panel(panel_index)) {
    return reload_archive_virtual_directory_for_panel(panel_index);
  }

  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return false;
  }

  const PanelController::SelectionSnapshot snapshot =
      panel.capture_selection_snapshot();
  const PanelController::ScrollPositionSnapshot scroll_snapshot =
      panel.capture_scroll_position();
  panel.model->reload();
  panel.restore_selection_snapshot(snapshot);
  panel.restore_scroll_position(scroll_snapshot);
  if (panel.ui.details_view != nullptr) {
    panel.ui.details_view->refresh_hover_from_cursor();
  }
  rebind_auto_refresh_watcher_for_panel(panel_index);
  update_status_for_panel(panel_index);
  return true;
}

void MainWindow::refresh_directory() {
  bool any = false;
  for (int i = 0; i < 2; ++i) {
    any = refresh_directory_for_panel(i) || any;
  }
  if (any) {
    for (int i = 0; i < 2; ++i) {
      update_status_for_panel(i);
    }
    refresh_action_states();
  }
}

void MainWindow::ensure_auto_refresh_watcher_for_panel(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.runtime.auto_refresh_watcher != nullptr) {
    return;
  }

  panel.runtime.auto_refresh_watcher = new QFileSystemWatcher(this);
  connect(panel.runtime.auto_refresh_watcher,
          &QFileSystemWatcher::directoryChanged,
          this,
          [this, panel_index](const QString&) {
            mark_panel_auto_refresh_dirty(panel_index);
          });
  connect(panel.runtime.auto_refresh_watcher,
          &QFileSystemWatcher::fileChanged,
          this,
          [this, panel_index](const QString&) {
            mark_panel_auto_refresh_dirty(panel_index);
          });
}

void MainWindow::rebind_auto_refresh_watcher_for_panel(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  panel.clear_auto_refresh_binding();

  const bool auto_refresh_enabled =
      auto_refresh_action_ != nullptr && auto_refresh_action_->isChecked();
  if (!auto_refresh_enabled || panel.model == nullptr || panel.in_archive_view()) {
    return;
  }

  const QString watched_dir = QFileInfo(panel.model->directory()).absoluteFilePath();
  const QFileInfo watched_info(watched_dir);
  if (watched_dir.isEmpty() || !watched_info.exists() || !watched_info.isDir()) {
    return;
  }

  ensure_auto_refresh_watcher_for_panel(panel_index);
  if (panel.runtime.auto_refresh_watcher == nullptr) {
    return;
  }

  if (panel.runtime.auto_refresh_watcher->addPath(watched_dir)) {
    panel.runtime.auto_refresh_watched_dir = watched_dir;
  }
}

void MainWindow::mark_panel_auto_refresh_dirty(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.in_archive_view()) {
    return;
  }
  panel.mark_auto_refresh_dirty();
}

void MainWindow::on_auto_refresh_timer_tick() {
  bool any = false;
  for (int i = 0; i < 2; ++i) {
    PanelController& panel = panel_controller(i);
    if (panel.model == nullptr || panel.in_archive_view()) {
      continue;
    }

    const QString current_dir = QFileInfo(panel.model->directory()).absoluteFilePath();
    const bool was_dirty = panel.runtime.auto_refresh_dirty;
    if (panel.auto_refresh_needs_rebind(current_dir)) {
      rebind_auto_refresh_watcher_for_panel(i);
      if (was_dirty) {
        panel.mark_auto_refresh_dirty();
      }
    }

    if (!panel.runtime.auto_refresh_dirty) {
      continue;
    }
    if (refresh_directory_for_panel(i)) {
      panel.runtime.auto_refresh_dirty = false;
      any = true;
    }
  }

  if (any) {
    update_status();
  }
}

void MainWindow::set_current_directory_for_panel(int panel_index, const QString& path) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return;
  }

  if (panel.in_archive_view()) {
    load_archive_virtual_directory_for_panel(
        panel_index,
        panel.archive.source_archive,
        path,
        panel.archive.origin_dir,
        panel.archive.type_hint,
        false,
        {},
        false,
        {},
        panel.archive.current_token,
        panel.archive_display_source());
    return;
  }

  const QFileInfo info(path);
  if (!info.exists() || !info.isDir()) {
    return;
  }

  const QString abs_path = info.absoluteFilePath();
  panel.model->set_directory(abs_path);
  rebind_auto_refresh_watcher_for_panel(panel_index);
  apply_archive_preview_columns_visibility_for_panel(panel_index);
  remember_folder_history(abs_path);

  if (panel.ui.details_view != nullptr && panel.ui.details_view->selectionModel() != nullptr) {
    panel.ui.details_view->selectionModel()->clearSelection();
  }

  if (panel_index == active_panel_index_) {
    refresh_active_panel_chrome();
  } else {
    update_status_for_panel(panel_index);
  }
}

void MainWindow::set_current_directory(const QString& path) {
  set_current_directory_for_panel(active_panel_index_, path);
}

QString MainWindow::current_directory_for_panel(int panel_index) const {
  return panel_controller(panel_index).current_directory();
}

QString MainWindow::current_directory() const {
  return current_directory_for_panel(active_panel_index_);
}

QString MainWindow::focused_comment_for_panel(int panel_index) const {
  const PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.focused_item_is_parent_link()) {
    return {};
  }

  const QString focused_path = panel.focused_path();
  if (focused_path.trimmed().isEmpty()) {
    return {};
  }

  const int row_count = panel.model->rowCount();
  for (int row = 0; row < row_count; ++row) {
    if (panel.model->path_for_row(row) == focused_path) {
      return panel.model
          ->data(panel.model->index(row, DirectoryListModel::kCommentColumn),
                 Qt::DisplayRole)
          .toString();
    }
  }
  return {};
}

QStringList MainWindow::selected_filesystem_paths_including_parent_link_for_panel(int panel_index) const {
  return panel_controller(panel_index).selected_filesystem_paths_including_parent_link();
}

QStringList MainWindow::selected_filesystem_paths_including_parent_link() const {
  return selected_filesystem_paths_including_parent_link_for_panel(active_panel_index_);
}

QStringList MainWindow::selected_real_archive_file_paths_for_panel(int panel_index) const {
  return panel_controller(panel_index).selected_real_archive_file_paths();
}

bool MainWindow::selected_rows_include_parent_link_for_panel(int panel_index) const {
  return panel_controller(panel_index).selected_rows_include_parent_link();
}

bool MainWindow::active_selected_rows_include_parent_link() const {
  return selected_rows_include_parent_link_for_panel(active_panel_index_);
}

bool MainWindow::has_selected_real_items_for_panel(int panel_index) const {
  return !panel_controller(panel_index).selected_real_item_rows().isEmpty();
}

bool MainWindow::has_selected_real_items() const {
  return has_selected_real_items_for_panel(active_panel_index_);
}

bool MainWindow::has_oper_smart_real_items_for_panel(int panel_index) const {
  return !panel_controller(panel_index).oper_smart_real_item_rows().isEmpty();
}

bool MainWindow::has_oper_smart_real_items() const {
  return has_oper_smart_real_items_for_panel(active_panel_index_);
}

QString MainWindow::selected_archive_path() const {
  const QStringList paths = selected_filesystem_paths_including_parent_link();
  if (paths.size() != 1) {
    return QString();
  }

  const QFileInfo info(paths.front());
  if (!info.isFile()) {
    return QString();
  }
  return info.absoluteFilePath();
}

QStringList MainWindow::selected_archive_entries() const {
  return active_panel_controller().selected_archive_entries();
}

bool MainWindow::in_archive_view() const {
  return in_archive_view_for_panel(active_panel_index_);
}

bool MainWindow::in_archive_view_for_panel(int panel_index) const {
  return panel_controller(panel_index).in_archive_view();
}

}  // namespace z7::ui::filemanager
