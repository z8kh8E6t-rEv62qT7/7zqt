// src/ui/filemanager/src/main_window/core/core_open.cpp
// Role: Focus resolution and open dispatch logic.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

namespace {

bool source_rows_contain_row(const QModelIndexList& rows, int source_row) {
  for (const QModelIndex& row : rows) {
    if (row.isValid() && row.row() == source_row) {
      return true;
    }
  }
  return false;
}

}  // namespace

QString MainWindow::focused_path_for_panel(int panel_index) const {
  return panel_controller(panel_index).focused_path();
}

MainWindow::ArchiveOpenSelectionTarget
MainWindow::resolve_archive_open_selection_target(int panel_index) const {
  ArchiveOpenSelectionTarget target;
  if (!in_archive_view_for_panel(panel_index)) {
    return target;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return target;
  }

  const QModelIndexList rows = panel.selected_rows_including_parent_link();
  target.entries.reserve(rows.size());
  for (const QModelIndex& row : rows) {
    if (!row.isValid()) {
      continue;
    }

    const int row_index = row.row();
    if (panel.model->is_parent_link_for_row(row_index)) {
      continue;
    }

    const QString entry_path = z7::ui::archive_support::normalize_virtual_dir(
        panel.model->path_for_row(row_index));
    if (entry_path.isEmpty() || target.entries.contains(entry_path)) {
      continue;
    }

    target.entries << entry_path;
    if (target.entries.size() == 1) {
      target.single_entry_path = entry_path;
      target.single_entry_is_dir = panel.model->is_dir_for_row(row_index);
      continue;
    }

    target.single_entry_path.clear();
    target.single_entry_is_dir = false;
  }

  if (target.entries.size() != 1) {
    target.single_entry_path.clear();
    target.single_entry_is_dir = false;
  }
  return target;
}

void MainWindow::open_item(const QString& path,
                           bool try_internal,
                           bool try_external,
                           const QString& archive_type_hint) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return;
  }

  if (info.isDir()) {
    if (try_internal) {
      set_current_directory(info.absoluteFilePath());
    } else if (try_external) {
      open_path_externally_untracked(info.absoluteFilePath());
    }
    return;
  }

  if (try_internal) {
    const bool force_internal = !archive_type_hint.isEmpty();
    const bool prefer_external =
        !force_internal && try_external &&
        should_always_start_externally(info.absoluteFilePath());
    if (!prefer_external) {
      std::function<void()> failure_fallback;
      if (!force_internal && try_external) {
        const QString fallback_path = info.absoluteFilePath();
        failure_fallback = [this, fallback_path]() {
          open_path_externally_untracked(fallback_path);
        };
      }
      open_archive_inside_for_panel(active_panel_index_,
                                    info.absoluteFilePath(),
                                    archive_type_hint,
                                    std::move(failure_fallback));
      return;
    }
  }

  if (try_external) {
    open_path_externally_untracked(info.absoluteFilePath());
  }
}

void MainWindow::open_focused_item_as_internal(const QString& archive_type_hint) {
  if (in_archive_view()) {
    PanelController& panel = active_panel_controller();
    if (panel.model == nullptr) {
      return;
    }

    if (panel.focused_item_is_parent_link()) {
      on_open_parent_requested();
      return;
    }

    const QString entry_path = panel.focused_path();
    if (entry_path.isEmpty()) {
      return;
    }

    if (!panel.focused_item_is_dir()) {
      open_archive_file_inside_for_panel(active_panel_index_,
                                         entry_path,
                                         archive_type_hint);
      return;
    }

    load_archive_virtual_directory_for_panel(
        active_panel_index_,
        panel.archive.source_archive,
        entry_path,
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

  const QString focused_path = focused_path_for_panel(active_panel_index_);
  if (focused_path.isEmpty()) {
    return;
  }

  const QFileInfo info(focused_path);
  if (info.isDir()) {
    set_current_directory(info.absoluteFilePath());
    return;
  }

  open_item(info.absoluteFilePath(), true, false, archive_type_hint);
}

void MainWindow::open_selected_archive_entries(bool try_internal) {
  PanelController& panel = active_panel_controller();
  if (!in_archive_view() || panel.model == nullptr) {
    return;
  }

  const ArchiveOpenSelectionTarget selection =
      resolve_archive_open_selection_target(active_panel_index_);
  const QStringList& entries = selection.entries;
  if (entries.isEmpty()) {
    return;
  }
  if (entries.size() > 20) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540)), z7::ui::runtime_support::L(3016));
    return;
  }

  if (!try_internal) {
    open_archive_entries_outside_for_panel(active_panel_index_, entries);
    return;
  }

  if (entries.size() > 1) {
    open_archive_entries_outside_for_panel(active_panel_index_, entries);
    return;
  }

  if (selection.single_entry_path.isEmpty()) {
    return;
  }

  if (selection.single_entry_is_dir) {
    load_archive_virtual_directory_for_panel(
        active_panel_index_,
        panel.archive.source_archive,
        selection.single_entry_path,
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

  const bool prefer_external =
      should_always_start_externally(selection.single_entry_path);
  if (prefer_external) {
    open_archive_entries_outside_for_panel(active_panel_index_, entries);
    return;
  }

  open_archive_file_inside_for_panel(active_panel_index_,
                                     selection.single_entry_path,
                                     QString());
}

void MainWindow::open_selected_filesystem_paths_including_parent_link(
    bool try_internal) {
  if (in_archive_view()) {
    return;
  }

  const PanelController& panel = active_panel_controller();
  if (panel.model == nullptr) {
    return;
  }

  const QModelIndexList selected_rows =
      panel.selected_rows_including_parent_link();
  if (selected_rows.isEmpty()) {
    return;
  }

  const QModelIndexList real_rows = panel.selected_real_item_rows();
  if (real_rows.size() > 20) {
    QMessageBox::information(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540)), z7::ui::runtime_support::L(3016));
    return;
  }

  QStringList paths;
  paths.reserve(real_rows.size() + 1);
  const QModelIndex focused = panel.focused_source_index();
  const bool focused_is_selected =
      focused.isValid() && source_rows_contain_row(selected_rows, focused.row());
  const bool focused_parent_selected =
      focused_is_selected && panel.model->is_parent_link_for_row(focused.row());
  if (focused_parent_selected && try_internal) {
    on_open_parent_requested();
    return;
  }
  if (focused_parent_selected && real_rows.isEmpty()) {
    const QString parent_path = panel.model->path_for_row(focused.row());
    if (!parent_path.trimmed().isEmpty()) {
      paths << parent_path;
    }
  }

  for (const QModelIndex& row : real_rows) {
    if (!row.isValid()) {
      continue;
    }
    const QString path = panel.model->path_for_row(row.row());
    if (!path.trimmed().isEmpty()) {
      paths << path;
    }
  }

  if (paths.isEmpty()) {
    return;
  }

  bool dir_started = false;
  for (const QString& path : paths) {
    const QFileInfo info(path);
    if (!info.exists()) {
      continue;
    }

    if (info.isDir()) {
      if (dir_started) {
        continue;
      }

      if (try_internal) {
        set_current_directory(info.absoluteFilePath());
        dir_started = true;
        break;
      }

      open_path_externally_untracked(info.absoluteFilePath());
      continue;
    }

    open_item(info.absoluteFilePath(), try_internal && paths.size() == 1, true);
  }
}


}  // namespace z7::ui::filemanager
