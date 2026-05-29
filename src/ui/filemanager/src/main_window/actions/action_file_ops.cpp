// src/ui/filemanager/src/main_window/actions/action_file_ops.cpp
// Role: File-operation handlers (rename/copy/move/delete) and transfer helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

namespace {

QStringList build_archive_move_delete_entries(
    const z7::app::ExtractResult& extract_result) {
  QSet<QString> selected_entries;
  QStringList ordered_directories;
  QStringList delete_entries;

  for (const z7::app::ExtractMaterializedEntry& entry :
       extract_result.materialized_entries) {
    const QString archive_entry =
        z7::ui::archive_support::normalize_virtual_dir(
            z7::ui::archive_support::from_utf8_string(entry.archive_entry_path));
    if (archive_entry.isEmpty() || selected_entries.contains(archive_entry)) {
      continue;
    }

    selected_entries.insert(archive_entry);
    if (entry.is_directory) {
      ordered_directories << archive_entry;
      continue;
    }
    delete_entries << archive_entry;
  }

  for (const QString& directory_entry : ordered_directories) {
    const QString child_prefix = directory_entry + QLatin1Char('/');
    bool has_materialized_child = false;
    for (const QString& candidate : selected_entries) {
      if (candidate.size() > child_prefix.size() &&
          candidate.startsWith(child_prefix)) {
        has_materialized_child = true;
        break;
      }
    }
    if (!has_materialized_child) {
      delete_entries << directory_entry;
    }
  }

  return delete_entries;
}

QString archive_entry_leaf_name(const QString& archive_entry) {
  const QString normalized =
      z7::ui::archive_support::normalize_virtual_dir(archive_entry);
  const int slash = normalized.lastIndexOf(QLatin1Char('/'));
  if (slash < 0) {
    return normalized;
  }
  return normalized.mid(slash + 1);
}

QString archive_entry_parent_dir(const QString& archive_entry) {
  const QString normalized =
      z7::ui::archive_support::normalize_virtual_dir(archive_entry);
  const int slash = normalized.lastIndexOf(QLatin1Char('/'));
  if (slash <= 0) {
    return QString();
  }
  return normalized.left(slash);
}

QString existing_entry_identity_path(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return QString();
  }

  const QString canonical = info.canonicalFilePath();
  if (!canonical.isEmpty()) {
    return QDir::cleanPath(canonical);
  }
  return QDir::cleanPath(info.absoluteFilePath());
}

bool paths_refer_to_same_existing_entry(const QString& left,
                                        const QString& right) {
  const QString left_identity = existing_entry_identity_path(left);
  const QString right_identity = existing_entry_identity_path(right);
  return !left_identity.isEmpty() && left_identity == right_identity;
}

QString non_empty_label(uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(
             z7::ui::runtime_support::L(id))
      .trimmed();
}

QString localized_size_bytes(uint64_t value) {
  QString bytes_format =
      z7::ui::runtime_support::strip_mnemonic(
          z7::ui::runtime_support::L(3504))
          .trimmed();
  const QString number = QString::number(value);
  if (bytes_format.contains(QStringLiteral("{0}"))) {
    bytes_format.replace(QStringLiteral("{0}"), number);
    return bytes_format;
  }
  return QStringLiteral("%1 %2").arg(number, bytes_format);
}

void append_items_info_pair(QStringList* lines,
                            const QString& label,
                            uint64_t count,
                            bool size_defined,
                            uint64_t size) {
  if (lines == nullptr || count == 0) {
    return;
  }
  if (!size_defined) {
    lines->append(QStringLiteral("%1: %2")
                      .arg(label, QString::number(count)));
    return;
  }
  lines->append(QStringLiteral("%1: %2    ( %3 )")
                    .arg(label,
                         QString::number(count),
                         localized_size_bytes(size)));
}

QString normalize_transfer_destination_for_compare(const QString& value) {
  QString out = QDir::fromNativeSeparators(value.trimmed());
  while (out.size() > 1 && out.endsWith(QLatin1Char('/'))) {
    out.chop(1);
  }
  return QDir::cleanPath(out);
}

template <typename PanelLike>
bool transfer_destination_matches_archive_panel(const PanelLike& panel,
                                                const QString& raw_destination) {
  if (!panel.in_archive_view()) {
    return false;
  }
  const QString requested =
      normalize_transfer_destination_for_compare(raw_destination);
  const QString panel_path =
      normalize_transfer_destination_for_compare(
          panel.archive_virtual_display_path());
  return !requested.isEmpty() && requested == panel_path;
}

}  // namespace

void MainWindow::on_rename_requested() {
  (void)edit_focused_item_label_for_panel(active_panel_index_);
}

QString MainWindow::copy_move_info_text_for_source_rows(
    int panel_index,
    const QModelIndexList& rows) const {
  const PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || rows.isEmpty()) {
    return QStringLiteral("%1 item(s) selected.").arg(rows.size());
  }

  uint64_t dir_count = 0;
  uint64_t file_count = 0;
  uint64_t folders_size = 0;
  uint64_t files_size = 0;
  bool folders_size_defined = true;
  bool files_size_defined = true;
  int real_row_count = 0;
  QStringList preview_lines;
  constexpr int kPreviewLimit = 5;
  const QString current_virtual =
      z7::ui::archive_support::normalize_virtual_dir(panel.archive.virtual_dir);

  for (const QModelIndex& row_index : rows) {
    if (!row_index.isValid() ||
        panel.model->is_parent_link_for_row(row_index.row())) {
      continue;
    }

    ++real_row_count;
    const bool is_dir = panel.model->is_dir_for_row(row_index.row());
    if (is_dir) {
      ++dir_count;
    } else {
      ++file_count;
    }

    bool ok_size = false;
    const uint64_t item_size =
        panel.model
            ->data(panel.model->index(row_index.row(),
                                      DirectoryListModel::kSizeColumn),
                   Qt::DisplayRole)
            .toULongLong(&ok_size);
    if (!ok_size) {
      if (is_dir) {
        folders_size_defined = false;
      } else {
        files_size_defined = false;
      }
    } else if (is_dir) {
      folders_size += item_size;
    } else {
      files_size += item_size;
    }

    if (preview_lines.size() < kPreviewLimit) {
      QString line = panel.model->path_for_row(row_index.row());
      if (panel.in_archive_view()) {
        line = z7::ui::archive_support::normalize_virtual_dir(line);
        if (!current_virtual.isEmpty()) {
          const QString prefix = current_virtual + QLatin1Char('/');
          if (line == current_virtual) {
            line.clear();
          } else if (line.startsWith(prefix)) {
            line = line.mid(prefix.size());
          }
        }
      } else {
        line = QFileInfo(line).fileName();
      }
      line = QDir::toNativeSeparators(line);
      if (is_dir && !line.endsWith(QLatin1Char('/')) &&
          !line.endsWith(QLatin1Char('\\'))) {
        line += QDir::separator();
      }
      preview_lines << line;
    }
  }

  QString prefix_line = panel.in_archive_view()
                            ? panel.archive_virtual_display_path()
                            : QDir::toNativeSeparators(panel.current_directory());
  if (!prefix_line.isEmpty()) {
    const QChar sep = QDir::separator();
    if (!prefix_line.endsWith(sep)) {
      prefix_line += sep;
    }
  }

  QStringList lines;
  append_items_info_pair(&lines,
                         non_empty_label(1031),
                         dir_count,
                         folders_size_defined,
                         folders_size);
  append_items_info_pair(&lines,
                         non_empty_label(1032),
                         file_count,
                         files_size_defined,
                         files_size);
  const bool include_total_size =
      folders_size_defined && files_size_defined && folders_size != 0 &&
      files_size != 0;
  if (include_total_size) {
    lines << QStringLiteral("%1: %2")
                 .arg(non_empty_label(1007),
                      localized_size_bytes(folders_size + files_size));
  }
  lines << QString();
  lines << prefix_line;
  for (const QString& preview : preview_lines) {
    lines << QStringLiteral("  %1").arg(preview);
  }
  if (real_row_count > preview_lines.size()) {
    lines << QStringLiteral("  ...");
  }
  return lines.join(QLatin1Char('\n'));
}

bool MainWindow::select_model_path_for_panel(int panel_index,
                                             const QString& model_path) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr ||
      model_path.trimmed().isEmpty()) {
    return false;
  }

  int target_row = -1;
  const int row_count = panel.model->rowCount();
  for (int row = 0; row < row_count; ++row) {
    if (panel.model->path_for_row(row) == model_path) {
      target_row = row;
      break;
    }
  }
  if (target_row < 0) {
    return false;
  }

  const QModelIndex proxy_index = panel.map_source_to_proxy(
      panel.model->index(target_row, DirectoryListModel::kNameColumn));
  if (!proxy_index.isValid()) {
    return false;
  }

  QItemSelectionModel* selection_model = panel.ui.details_view->selectionModel();
  selection_model->clearSelection();
  selection_model->select(proxy_index,
                          QItemSelectionModel::Select |
                              QItemSelectionModel::Rows);
  selection_model->setCurrentIndex(proxy_index, QItemSelectionModel::NoUpdate);
  if (QAbstractItemView* view = panel.current_item_view(); view != nullptr) {
    view->scrollTo(proxy_index);
  }
  update_status_for_panel(panel_index);
  return true;
}

bool MainWindow::edit_focused_item_label_for_panel(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return false;
  }

  const QModelIndex focused = panel.focused_source_index();
  if (!focused.isValid() ||
      panel.model->is_parent_link_for_row(focused.row())) {
    return false;
  }

  const bool can_rename =
      panel.in_archive_view()
          ? panel.archive.current_token.is_valid()
          : QFileInfo(panel.current_directory()).isWritable();
  if (!can_rename) {
    return false;
  }

  const QModelIndex source_index =
      panel.model->index(focused.row(), DirectoryListModel::kNameColumn);
  if ((panel.model->flags(source_index) & Qt::ItemIsEditable) == 0) {
    return false;
  }

  QAbstractItemView* view = panel.current_item_view();
  if (view == nullptr) {
    return false;
  }

  const QModelIndex proxy_index = panel.map_source_to_proxy(source_index);
  if (!proxy_index.isValid()) {
    return false;
  }

  view->setFocus(Qt::OtherFocusReason);
  view->setCurrentIndex(proxy_index);
  view->scrollTo(proxy_index);
  view->edit(proxy_index);
  return true;
}

bool MainWindow::start_rename_item_for_panel(int panel_index,
                                             const QString& item_path,
                                             const QString& new_name,
                                             bool item_is_dir) {
  if (in_archive_view_for_panel(panel_index)) {
    return start_rename_archive_entry_in_preview(panel_index,
                                                 item_path,
                                                 new_name,
                                                 item_is_dir);
  }
  return start_rename_filesystem_item_for_panel(panel_index,
                                                item_path,
                                                new_name);
}

bool MainWindow::start_rename_filesystem_item_for_panel(
    int panel_index,
    const QString& source_path,
    const QString& new_name) {
  if (in_archive_view_for_panel(panel_index)) {
    return false;
  }
  const PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || source_path.trimmed().isEmpty()) {
    return false;
  }

  const QFileInfo src(source_path);
  if (!src.exists() || !QFileInfo(src.absolutePath()).isWritable()) {
    return false;
  }

  QString normalized_name;
  QString error_message;
  const QString title =
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(545));
  if (!validate_basename_only_name(new_name, &normalized_name, &error_message)) {
    QMessageBox::warning(this, title, error_message);
    return false;
  }

  if (normalized_name == src.fileName()) {
    return true;
  }

  const QString dst_path = src.dir().filePath(normalized_name);
  if (QFileInfo::exists(dst_path)) {
    QMessageBox::warning(this,
                         title,
                         z7::ui::runtime_support::LF(3008, {dst_path}));
    return false;
  }

  const QString absolute_source_path = src.absoluteFilePath();
  const QString old_path = absolute_source_path;
  const QString new_path = QFileInfo(dst_path).absoluteFilePath();
  return start_task_with_runner(
      QStringLiteral("%1: %2").arg(title, absolute_source_path),
      title,
      [absolute_source_path, normalized_name](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_rename_path(absolute_source_path,
                                         normalized_name);
      },
      [this, panel_index, old_path, new_path](
          bool ok,
          int,
          int,
          const QString&,
          const z7::app::OperationOutcome&) {
        refresh_directory();
        (void)select_model_path_for_panel(panel_index,
                                          ok ? new_path : old_path);
      });
}

void MainWindow::select_archive_entry_for_panel(int panel_index,
                                                const QString& archive_entry) {
  if (!in_archive_view_for_panel(panel_index)) {
    return;
  }
  const QString normalized_entry =
      z7::ui::archive_support::normalize_virtual_dir(archive_entry);
  (void)select_model_path_for_panel(panel_index, normalized_entry);
}

bool MainWindow::start_rename_archive_entry_in_preview(
    int panel_index,
    const QString& archive_entry,
    const QString& new_name,
    bool entry_is_dir) {
  if (!in_archive_view_for_panel(panel_index)) {
    return false;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (!panel.archive.current_token.is_valid()) {
    return false;
  }

  const QString normalized_entry =
      z7::ui::archive_support::normalize_virtual_dir(archive_entry);
  if (normalized_entry.isEmpty()) {
    return false;
  }

  QString normalized_name;
  QString error_message;
  const QString title =
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(545));
  if (!validate_basename_only_name(new_name, &normalized_name, &error_message)) {
    QMessageBox::warning(this, title, error_message);
    return false;
  }

  if (normalized_name == archive_entry_leaf_name(normalized_entry)) {
    return true;
  }

  const QString new_entry = z7::ui::archive_support::join_virtual_path(
      archive_entry_parent_dir(normalized_entry),
      normalized_name);
  const int row_count = panel.model->rowCount();
  for (int row = 0; row < row_count; ++row) {
    if (panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    const QString candidate =
        z7::ui::archive_support::normalize_virtual_dir(
            panel.model->path_for_row(row));
    if (candidate == new_entry) {
      QMessageBox::warning(
          this,
          title,
          z7::ui::runtime_support::LF(3008, {normalized_name}));
      return false;
    }
  }

  const QString archive_path = panel.archive.source_archive;
  const QString archive_display_source = panel.archive_display_source();
  const z7::app::ArchiveSessionToken session_token = panel.archive.current_token;
  return start_task_with_runner(
      QStringLiteral("%1: %2").arg(title, QDir::toNativeSeparators(archive_path)),
      title,
      [archive_path,
       session_token,
       normalized_entry,
       normalized_name,
       entry_is_dir](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_rename_archive_entry(archive_path,
                                                  session_token,
                                                  normalized_entry,
                                                  normalized_name,
                                                  entry_is_dir);
      },
      [this,
       panel_index,
       archive_path,
       archive_display_source,
       session_token,
       normalized_entry,
       new_entry](bool ok,
                  int,
                  int,
                  const QString&,
                  const z7::app::OperationOutcome&) {
        const QString focus_entry = ok ? new_entry : normalized_entry;
        if (!reload_archive_virtual_directory_for_panel(
                panel_index,
                [this, panel_index, focus_entry](bool loaded) {
                  if (loaded) {
                    select_archive_entry_for_panel(panel_index, focus_entry);
                  }
                })) {
          if (ok) {
            reload_matching_archive_writeback_panels(archive_path,
                                                    archive_display_source,
                                                    session_token);
          }
        }
      });
}

void MainWindow::on_copy_to_requested() {
  run_copy_or_move(false);
}

void MainWindow::on_move_to_requested() {
  run_copy_or_move(true);
}

void MainWindow::on_delete_requested() {
  const auto ask_question = [this](const QString& title,
                                   const QString& message,
                                   QMessageBox::StandardButtons buttons,
                                   QMessageBox::StandardButton default_button) {
    if (question_box_) {
      return question_box_(title, message, buttons, default_button);
    }
    return QMessageBox::question(this, title, message, buttons, default_button);
  };

  if (in_archive_view()) {
    const int panel_index = active_panel_index_;
    const PanelController& panel = active_panel_controller();
    if (panel.ui.details_view == nullptr || panel.ui.details_view->selectionModel() == nullptr ||
        panel.model == nullptr) {
      return;
    }

    const QModelIndexList rows = panel.selected_rows_including_parent_link();

    QStringList entries;
    entries.reserve(rows.size());
    QString single_entry_name;
    bool single_entry_is_dir = false;
    for (const QModelIndex& row : rows) {
      if (!row.isValid()) {
        continue;
      }
      const int row_index = row.row();
      if (panel.model->is_parent_link_for_row(row_index)) {
        continue;
      }

      const QString rel = z7::ui::archive_support::normalize_virtual_dir(
          panel.model->path_for_row(row_index));
      if (rel.isEmpty() || entries.contains(rel)) {
        continue;
      }

      entries << rel;
      if (entries.size() == 1) {
        single_entry_is_dir = panel.model->is_dir_for_row(row_index);
        single_entry_name =
            panel.model->index(row_index, 0).data(Qt::DisplayRole).toString().trimmed();
        if (single_entry_name.isEmpty()) {
          single_entry_name = QFileInfo(rel).fileName();
        }
        if (single_entry_name.isEmpty()) {
          single_entry_name = rel;
        }
      }
    }

    if (entries.isEmpty()) {
      return;
    }

    QString title;
    QString message;
    if (entries.size() == 1) {
      if (single_entry_name.isEmpty()) {
        single_entry_name = entries.front();
      }
      if (single_entry_is_dir) {
        title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6101));
        message = z7::ui::runtime_support::LF(6104, {single_entry_name});
      } else {
        title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6100));
        message = z7::ui::runtime_support::LF(6103, {single_entry_name});
      }
    } else {
      title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6102));
      message = z7::ui::runtime_support::LF(6105, {QString::number(entries.size())});
    }

    const auto reply = ask_question(title,
                                    message,
                                    QMessageBox::Yes | QMessageBox::No |
                                        QMessageBox::Cancel,
                                    QMessageBox::Yes);
    if (reply != QMessageBox::Yes) {
      return;
    }

    const QString archive_path = active_panel_controller().archive.source_archive;
    const z7::app::ArchiveSessionToken session_token =
        active_panel_controller().archive.current_token;
    start_task_with_runner(
        QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)), archive_path),
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)),
        [archive_path, entries, session_token](ArchiveProcessRunner* runner) {
          return runner != nullptr &&
                 runner->start_delete_entries(
                     archive_path,
                     entries,
                     session_token);
        },
        [this,
         panel_index,
         session_token](bool ok_delete,
                        int,
                        int,
                        const QString&,
                        const z7::app::OperationOutcome&) {
          if (!ok_delete) {
            return;
          }
          const PanelController& panel = panel_controller(panel_index);
          load_archive_virtual_directory_for_panel(
              panel_index,
              panel.archive.source_archive,
              panel.archive.virtual_dir,
              panel.archive.origin_dir,
              panel.archive.type_hint,
              false,
              {},
              false,
              {},
              session_token,
              panel.archive_display_source());
        });
    return;
  }

  if (active_selected_rows_include_parent_link()) {
    return;
  }

  const QStringList paths = active_panel_controller().selected_real_item_paths();
  if (paths.isEmpty()) {
    return;
  }
  const bool to_recycle_bin =
      (QApplication::keyboardModifiers() & Qt::ShiftModifier) == 0;

  QString title;
  QString message;

  if (paths.size() == 1) {
    const QFileInfo info(paths.front());
    if (info.isDir()) {
      title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6101));
      message = z7::ui::runtime_support::LF(6104, {info.fileName()});
    } else {
      title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6100));
      message = z7::ui::runtime_support::LF(6103, {info.fileName()});
    }
  } else {
    title = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6102));
    message = z7::ui::runtime_support::LF(6105, {QString::number(paths.size())});
  }

  if (confirm_delete_) {
    const auto reply = ask_question(title,
                                    message,
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (reply != QMessageBox::Yes) {
      return;
    }
  }

  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)), QString::number(paths.size())),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)),
      [paths, to_recycle_bin](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_delete_paths(paths, to_recycle_bin);
      },
      [this](bool ok, int, int, const QString&, const z7::app::OperationOutcome&) {
        if (ok) {
          refresh_directory();
        }
      });
}

bool MainWindow::run_filesystem_transfer_to_archive_panel(
    int target_panel_index,
    const QStringList& sources,
    bool move,
    const QString& history_path,
    const QString& caption) {
  if (sources.isEmpty() ||
      !can_add_external_files_to_archive_preview(target_panel_index)) {
    return false;
  }

  const PanelController& target_panel = panel_controller(target_panel_index);
  const ArchiveWritebackPlan writeback_plan =
      build_archive_writeback_plan_for_panel(target_panel_index);
  if (!writeback_plan.is_valid() ||
      !target_panel.archive.current_token.is_valid()) {
    return false;
  }

  QVector<ArchiveAddInputItem> input_items;
  input_items.reserve(sources.size());
  const QString target_virtual_dir =
      z7::ui::archive_support::normalize_virtual_dir(
          target_panel.archive.virtual_dir);
  for (const QString& source : sources) {
    const QFileInfo source_info(source);
    if (!source_info.exists() ||
        (!source_info.isFile() && !source_info.isDir())) {
      continue;
    }
    const QString target_entry =
        z7::ui::archive_support::join_virtual_path(
            target_virtual_dir,
            source_info.fileName());
    if (target_entry.isEmpty()) {
      continue;
    }
    input_items.push_back(
        ArchiveAddInputItem{source_info.absoluteFilePath(), target_entry});
  }
  if (input_items.isEmpty()) {
    return false;
  }

  const QString normalized_history = history_path.trimmed();
  if (!normalized_history.isEmpty()) {
    save_copy_history(
        normalize_copy_history(read_copy_history(), normalized_history));
  }

  const QStringList sources_to_delete = sources;
  const z7::app::ArchiveSessionToken session_token =
      target_panel.archive.current_token;
  return start_add_mapped_files_to_archive_preview(
      writeback_plan,
      session_token,
      input_items,
      caption,
      true,
      [this, move, sources_to_delete, caption](
          bool ok,
          int,
          int,
          const QString&,
          const z7::app::OperationOutcome&) {
        if (!move || !ok) {
          return;
        }
        QTimer::singleShot(0, this, [this, sources_to_delete, caption]() {
          start_task_with_runner(
              QStringLiteral("%1: %2")
                  .arg(caption, QString::number(sources_to_delete.size())),
              caption,
              [sources_to_delete](ArchiveProcessRunner* runner) {
                return runner != nullptr &&
                       runner->start_delete_paths(sources_to_delete, false);
              },
              [this](bool ok,
                     int,
                     int,
                     const QString&,
                     const z7::app::OperationOutcome&) {
                if (ok) {
                  refresh_directory();
                }
              });
        });
      });
}

void MainWindow::run_copy_or_move(bool move, bool copy_to_same) {
  if (in_archive_view()) {
    if (copy_to_same) {
      run_copy_or_move_archive_same_folder(move);
    } else {
      run_copy_or_move_archive_context(move);
    }
    return;
  }

  if (!copy_to_same && active_selected_rows_include_parent_link()) {
    return;
  }

  QModelIndexList source_rows;
  QStringList sources;
  QString default_destination = default_target_directory_for_transfer();
  if (copy_to_same) {
    const PanelController& panel = active_panel_controller();
    const QModelIndex focused = panel.focused_source_index();
    if (!focused.isValid() || panel.focused_item_is_parent_link()) {
      return;
    }
    const QString focused_path = panel.model != nullptr
                                     ? panel.model->path_for_row(focused.row())
                                     : QString();
    if (focused_path.isEmpty()) {
      return;
    }
    source_rows << panel.model->index(focused.row(),
                                      DirectoryListModel::kNameColumn);
    sources << focused_path;
    default_destination = QFileInfo(focused_path).fileName();
  } else {
    source_rows = active_panel_controller().selected_real_item_rows();
    sources = active_panel_controller().real_item_paths_for_rows(source_rows);
  }
  if (sources.isEmpty()) {
    QMessageBox::information(this,
                             z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6001) : z7::ui::runtime_support::L(6000)),
                             z7::ui::runtime_support::L(3015));
    return;
  }

  const QString operation_caption = z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6001) : z7::ui::runtime_support::L(6000));
  const CopyMoveDialogResult dialog_result = show_copy_move_dialog(
      this,
      operation_caption,
      z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6003) : z7::ui::runtime_support::L(6002)),
      copy_move_info_text_for_source_rows(active_panel_index_, source_rows),
      default_destination);
  if (!dialog_result.accepted) {
    return;
  }

  if (!copy_to_same && two_panels_visible_) {
    const int other_panel_index = 1 - active_panel_index_;
    const PanelController& other_panel = panel_controller(other_panel_index);
    if (transfer_destination_matches_archive_panel(
            other_panel,
            dialog_result.destination_path)) {
      if (!run_filesystem_transfer_to_archive_panel(
              other_panel_index,
              sources,
              move,
              other_panel.archive_virtual_display_path(),
              operation_caption)) {
        QMessageBox::information(this,
                                 operation_caption,
                                 z7::ui::runtime_support::L(3015));
      }
      return;
    }
  }

  const CopyTransferDestinationPlan destination_plan =
      build_copy_transfer_destination_plan(
          dialog_result.destination_path,
          current_directory_for_panel(active_panel_index_),
          sources.size(),
          false);
  if (!destination_plan.valid) {
    return;
  }

  auto destination_for_source = [&destination_plan](const QString& source) {
    if (!destination_plan.destination_path.isEmpty()) {
      return destination_plan.destination_path;
    }
    return QDir(destination_plan.destination_dir)
        .filePath(QFileInfo(source).fileName());
  };

  QStringList sources_to_apply = sources;
  for (const QString& src : sources_to_apply) {
    if (paths_refer_to_same_existing_entry(src, destination_for_source(src))) {
      QMessageBox::warning(
          this,
          operation_caption,
          QStringLiteral("Cannot copy or move files onto themselves."));
      return;
    }
  }

  if (!ensure_copy_transfer_destination_directories(destination_plan)) {
    QMessageBox::warning(
        this,
        operation_caption,
        QStringLiteral("Cannot create destination directory:\n%1")
            .arg(QDir::toNativeSeparators(destination_plan.display_path)));
    return;
  }

  save_copy_history(
      normalize_copy_history(read_copy_history(), destination_plan.history_path));

  const QString destination_display = QDir::toNativeSeparators(
      destination_plan.display_path);
  start_task_with_runner(
      QStringLiteral("%1: %2").arg(operation_caption, destination_display),
      operation_caption,
      [move,
       sources_to_apply,
       destination_plan](ArchiveProcessRunner* runner) {
        if (runner == nullptr) {
          return false;
        }
        return move ? runner->start_move_paths(sources_to_apply,
                                               destination_plan.destination_dir,
                                               OverwriteMode::kAsk,
                                               destination_plan.destination_path)
                    : runner->start_copy_paths(sources_to_apply,
                                               destination_plan.destination_dir,
                                               OverwriteMode::kAsk,
                                               destination_plan.destination_path);
      },
      [this](bool ok, int, int, const QString&, const z7::app::OperationOutcome&) {
        if (ok) {
          refresh_directory();
        }
      });
}

void MainWindow::run_copy_or_move_archive_same_folder(bool move) {
  const int panel_index = active_panel_index_;
  if (!in_archive_view_for_panel(panel_index)) {
    return;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.focused_item_is_parent_link()) {
    return;
  }

  const QModelIndex focused = panel.focused_source_index();
  if (!focused.isValid() || panel.model->is_parent_link_for_row(focused.row())) {
    return;
  }

  const QString source_entry =
      z7::ui::archive_support::normalize_virtual_dir(
          panel.model->path_for_row(focused.row()));
  const QString source_leaf = archive_entry_leaf_name(source_entry);
  if (source_entry.isEmpty() || source_leaf.isEmpty()) {
    return;
  }

  const ArchiveWritebackPlan writeback_plan =
      build_archive_writeback_plan_for_panel(panel_index);
  if (!writeback_plan.is_valid() || !panel.archive.current_token.is_valid()) {
    return;
  }

  const bool source_is_dir = panel.model->is_dir_for_row(focused.row());
  const QString archive_path = panel.archive.source_archive;
  const QString archive_type_hint = panel.archive.type_hint.trimmed();
  const QString current_virtual_dir =
      z7::ui::archive_support::normalize_virtual_dir(panel.archive.virtual_dir);
  const z7::app::ArchiveSessionToken session_token =
      panel.archive.current_token;
  const QString operation_caption =
      z7::ui::runtime_support::strip_mnemonic(
          move ? z7::ui::runtime_support::L(6001)
               : z7::ui::runtime_support::L(6000));

  const CopyMoveDialogResult dialog_result = show_copy_move_dialog(
      this,
      operation_caption,
      z7::ui::runtime_support::strip_mnemonic(
          move ? z7::ui::runtime_support::L(6003)
               : z7::ui::runtime_support::L(6002)),
      copy_move_info_text_for_source_rows(
          panel_index,
          QModelIndexList{panel.model->index(focused.row(),
                                             DirectoryListModel::kNameColumn)}),
      source_leaf);
  if (!dialog_result.accepted) {
    return;
  }

  QString target_leaf;
  QString error_message;
  if (!validate_basename_only_name(dialog_result.destination_path,
                                   &target_leaf,
                                   &error_message)) {
    QMessageBox::warning(this, operation_caption, error_message);
    return;
  }

  const QString target_entry = z7::ui::archive_support::join_virtual_path(
      current_virtual_dir,
      target_leaf);
  if (target_entry.isEmpty()) {
    return;
  }

  const int row_count = panel.model->rowCount();
  for (int row = 0; row < row_count; ++row) {
    if (panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    const QString candidate =
        z7::ui::archive_support::normalize_virtual_dir(
            panel.model->path_for_row(row));
    if (candidate == target_entry) {
      QMessageBox::warning(this,
                           operation_caption,
                           z7::ui::runtime_support::LF(3008, {target_leaf}));
      return;
    }
  }

  const QSharedPointer<QTemporaryDir> temp_dir =
      create_archive_open_temporary_directory(operation_caption);
  if (temp_dir == nullptr) {
    return;
  }

  const QStringList entries{source_entry};
  start_archive_source_extract_task(
      QStringLiteral("%1: %2").arg(operation_caption, archive_path),
      operation_caption,
      archive_path,
      archive_type_hint,
      session_token,
      temp_dir->path(),
      OverwriteMode::kOverwrite,
      entries,
      [this,
       temp_dir,
       writeback_plan,
       move,
       source_entry,
       source_is_dir,
       target_entry,
       archive_path,
       session_token,
       operation_caption](bool ok_extract,
                          int,
                          int,
                          const QString&,
                          const z7::app::OperationOutcome& outcome) {
        if (!ok_extract) {
          return;
        }

        const QStringList extracted_paths =
            extracted_archive_entry_paths(temp_dir->path(), {source_entry});
        if (extracted_paths.isEmpty()) {
          QMessageBox::information(this,
                                   operation_caption,
                                   z7::ui::runtime_support::L(3015));
          return;
        }

        QStringList delete_entries;
        if (move) {
          if (const auto extract_result =
                  z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
              extract_result.has_value()) {
            delete_entries = build_archive_move_delete_entries(*extract_result);
          }
          if (delete_entries.isEmpty() && !source_is_dir) {
            delete_entries << source_entry;
          }
          if (delete_entries.isEmpty()) {
            QMessageBox::information(this,
                                     operation_caption,
                                     z7::ui::runtime_support::L(3015));
            return;
          }
        }

        QVector<ArchiveAddInputItem> input_items;
        input_items.push_back(
            ArchiveAddInputItem{extracted_paths.front(), target_entry});
        const QString archive_display_source =
            writeback_plan.current_display_source();
        const bool add_started = start_add_mapped_files_to_archive_preview(
            writeback_plan,
            session_token,
            input_items,
            operation_caption,
            !move,
            [this,
             temp_dir,
             move,
             archive_path,
             archive_display_source,
             delete_entries,
             session_token](bool ok_add,
                            int,
                            int,
                            const QString&,
                            const z7::app::OperationOutcome&) {
              if (!move || !ok_add) {
                return;
              }

              QTimer::singleShot(
                  0,
                  this,
                  [this,
                   archive_path,
                   archive_display_source,
                   delete_entries,
                   session_token]() {
                    start_task_with_runner(
                        QStringLiteral("%1: %2")
                            .arg(z7::ui::runtime_support::strip_mnemonic(
                                     z7::ui::runtime_support::L(6106)),
                                 archive_path),
                        z7::ui::runtime_support::strip_mnemonic(
                            z7::ui::runtime_support::L(6106)),
                        [archive_path, delete_entries, session_token](
                            ArchiveProcessRunner* runner) {
                          return runner != nullptr &&
                                 runner->start_delete_entries(
                                     archive_path,
                                     delete_entries,
                                     session_token);
                        },
                        [this,
                         archive_path,
                         archive_display_source,
                         session_token](bool ok_delete,
                                        int,
                                        int,
                                        const QString&,
                                        const z7::app::OperationOutcome&) {
                          if (!ok_delete) {
                            return;
                          }
                          reload_matching_archive_writeback_panels(
                              archive_path,
                              archive_display_source,
                              session_token);
                        });
                  });
            });
        if (!add_started) {
          QMessageBox::information(this,
                                   operation_caption,
                                   z7::ui::runtime_support::L(3015));
        }
      });
}

void MainWindow::run_copy_or_move_archive_context(bool move) {
  if (!move) {
    (void)run_archive_export_from_active_panel();
    return;
  }

  const int panel_index = active_panel_index_;
  const PanelController& panel = active_panel_controller();
  const QModelIndexList source_rows = panel.selected_real_item_rows();
  const QStringList entries = panel.archive_entries_for_source_rows(source_rows);
  if (entries.isEmpty()) {
    QMessageBox::information(this,
                             z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6001) : z7::ui::runtime_support::L(6000)),
                             z7::ui::runtime_support::L(3015));
    return;
  }

  const QString operation_caption = z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6001) : z7::ui::runtime_support::L(6000));
  const CopyMoveDialogResult dialog_result = show_copy_move_dialog(
      this,
      operation_caption,
      z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6003) : z7::ui::runtime_support::L(6002)),
      copy_move_info_text_for_source_rows(panel_index, source_rows),
      default_target_directory_for_transfer());
  if (!dialog_result.accepted) {
    return;
  }

  QString destination_dir =
      QDir::fromNativeSeparators(dialog_result.destination_path.trimmed());
  if (destination_dir.isEmpty()) {
    return;
  }
  if (QDir::isRelativePath(destination_dir)) {
    destination_dir =
        QDir(current_directory_for_panel(active_panel_index_))
            .absoluteFilePath(destination_dir);
  }

  save_copy_history(normalize_copy_history(read_copy_history(), destination_dir));

  const QStringList items = {
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3421)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3422)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3423)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3424)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3425))};

  bool ok = false;
  const QString choice = QInputDialog::getItem(
      this,
      z7::ui::runtime_support::strip_mnemonic(move ? z7::ui::runtime_support::L(6001) : z7::ui::runtime_support::L(6000)),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3420)),
      items,
      0,
      false,
      &ok);
  if (!ok) {
    return;
  }

  OverwriteMode mode = OverwriteMode::kAsk;
  const int choice_index = items.indexOf(choice);
  if (choice_index == 1) {
    mode = OverwriteMode::kOverwrite;
  } else if (choice_index == 2) {
    mode = OverwriteMode::kSkip;
  } else if (choice_index == 3) {
    mode = OverwriteMode::kRenameExtracted;
  } else if (choice_index == 4) {
    mode = OverwriteMode::kRenameExisting;
  }

  const QString archive_path = panel.archive.source_archive;
  const QString archive_type_hint = panel.archive.type_hint.trimmed();
  const z7::app::ArchiveSessionToken session_token = panel.archive.current_token;
  start_archive_source_extract_task(
      QStringLiteral("%1: %2").arg(operation_caption, archive_path),
      operation_caption,
      archive_path,
      archive_type_hint,
      session_token,
      destination_dir,
      mode,
      entries,
      [this,
       panel_index,
       move,
       archive_path,
       session_token](bool ok_copy,
                      int,
                      int,
                      const QString&,
                      const z7::app::OperationOutcome& outcome) {
        if (!move || !ok_copy) {
          return;
        }

        const auto extract_result =
            z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
        if (!extract_result.has_value()) {
          return;
        }

        const QStringList delete_entries =
            build_archive_move_delete_entries(*extract_result);
        if (delete_entries.isEmpty()) {
          return;
        }

        QTimer::singleShot(
            0,
             this,
             [this,
             panel_index,
             archive_path,
             delete_entries,
             session_token]() {
          start_task_with_runner(
              QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)), archive_path),
              z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6106)),
              [archive_path, delete_entries, session_token](
                  ArchiveProcessRunner* runner) {
                return runner != nullptr &&
                       runner->start_delete_entries(
                           archive_path,
                           delete_entries,
                           session_token);
              },
              [this,
               panel_index,
               session_token](bool ok_delete,
                              int,
                              int,
                              const QString&,
                              const z7::app::OperationOutcome&) {
                if (!ok_delete) {
                  return;
                }
                const PanelController& panel = panel_controller(panel_index);
                load_archive_virtual_directory_for_panel(
                    panel_index,
                    panel.archive.source_archive,
                    panel.archive.virtual_dir,
                    panel.archive.origin_dir,
                    panel.archive.type_hint,
                    false,
                    {},
                    false,
                    {},
                    session_token,
                    panel.archive_display_source());
              });
            });
      });
}

QString MainWindow::default_target_directory_for_transfer() const {
  QString default_dir;
  const int other_panel = 1 - active_panel_index_;
  if (two_panels_visible_) {
    if (!in_archive_view() && in_archive_view_for_panel(other_panel)) {
      const QString other_archive_dir =
          panel_controller(other_panel).archive_virtual_display_path();
      if (!other_archive_dir.isEmpty()) {
        default_dir = other_archive_dir;
      }
    } else if (!in_archive_view_for_panel(other_panel)) {
      const QString other_dir = current_directory_for_panel(other_panel);
      if (!other_dir.isEmpty()) {
        default_dir = other_dir;
      }
    }
  }

  if (default_dir.isEmpty()) {
    const PanelController& panel = active_panel_controller();
    if (in_archive_view()) {
      if (!panel.archive.origin_dir.isEmpty()) {
        default_dir = panel.archive.origin_dir;
      } else if (!panel.archive.source_archive.isEmpty()) {
        default_dir = QFileInfo(panel.archive.source_archive).absolutePath();
      }
    } else {
      default_dir = current_directory_for_panel(active_panel_index_);
    }
  }

  if (default_dir.isEmpty()) {
    default_dir = current_directory();
  }
  if (default_dir.isEmpty()) {
    default_dir = QDir::homePath();
  }
  return default_dir;
}

}  // namespace z7::ui::filemanager
