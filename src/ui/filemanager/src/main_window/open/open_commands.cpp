// src/ui/filemanager/src/main_window/open/open_commands.cpp
// Role: Open/view/edit command execution and diff target resolution.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <QDirIterator>

namespace z7::ui::filemanager {

namespace {

struct FilesystemDirectoryStats {
  uint64_t folders = 0;
  uint64_t files = 0;
  uint64_t size = 0;
};

FilesystemDirectoryStats collect_filesystem_directory_stats(
    const QString& root_path) {
  FilesystemDirectoryStats stats;
  QDirIterator it(root_path,
                  QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                      QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo child = it.fileInfo();
    if (child.isDir()) {
      ++stats.folders;
      continue;
    }
    if (child.isFile()) {
      ++stats.files;
      const qint64 size = child.size();
      if (size > 0) {
        stats.size += static_cast<uint64_t>(size);
      }
    }
  }
  return stats;
}

}  // namespace

void MainWindow::on_open_requested() {
  if (in_archive_view()) {
    if (activate_archive_parent_link_for_panel(active_panel_index_)) {
      return;
    }
    open_selected_archive_entries(true);
  } else {
    open_selected_filesystem_paths_including_parent_link(true);
  }
}

void MainWindow::on_open_inside_requested() {
  open_focused_item_as_internal();
}

void MainWindow::on_open_inside_one_requested() {
  open_focused_item_as_internal(QStringLiteral("*"));
}

void MainWindow::on_open_inside_parser_requested() {
  open_focused_item_as_internal(QStringLiteral("#"));
}

void MainWindow::on_open_outside_requested() {
  if (in_archive_view()) {
    open_selected_archive_entries(false);
  } else {
    open_selected_filesystem_paths_including_parent_link(false);
  }
}

bool MainWindow::run_external_command_with_targets(const QString& command_line,
                                                   const QStringList& targets,
                                                   const QString& working_dir,
                                                   QString* error_message,
                                                   bool controlled_process,
                                                   QProcess** started_process) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (started_process != nullptr) {
    *started_process = nullptr;
  }
  if (targets.isEmpty()) {
    return true;
  }
  if (!confirm_external_open_targets_safe(targets, lang_or(540))) {
    return false;
  }

  const ExternalCommandParts command = parse_external_command_line(command_line);
  if (command.program.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = lang_or(264);
    }
    return false;
  }

  QStringList args;
  if (!command.arguments.trimmed().isEmpty()) {
    args = QProcess::splitCommand(command.arguments);
  }
  args.append(targets);

  if (controlled_process) {
    QProcess* process = new QProcess(this);
    process->setWorkingDirectory(working_dir);
    process->start(command.program, args);
    if (!process->waitForStarted(5000)) {
      if (error_message != nullptr) {
        *error_message = lang_or(264);
      }
      process->deleteLater();
      return false;
    }
    if (started_process != nullptr) {
      *started_process = process;
    }
    return true;
  }

  qint64 pid = 0;
  const bool started =
      external_command_launcher_ != nullptr
          ? external_command_launcher_(command.program, args, working_dir, &pid)
          : QProcess::startDetached(command.program, args, working_dir, &pid);
  if (!started && error_message != nullptr) {
    *error_message = lang_or(264);
  }
  return started;
}

void MainWindow::run_archive_view_or_edit_with_command(const QString& command_line,
                                                       const QString& caption) {
  const PanelController& panel = active_panel_controller();
  if (!in_archive_view() || panel.archive.source_archive.isEmpty()) {
    return;
  }
  if (panel.focused_item_is_parent_link() || panel.focused_item_is_dir()) {
    return;
  }

  const QString focused_entry =
      z7::ui::archive_support::normalize_virtual_dir(panel.focused_path());
  if (focused_entry.isEmpty()) {
    return;
  }
  const QStringList entries{focused_entry};

  const QString archive_path = panel.archive.source_archive;
  const QString archive_type_hint = panel.archive.type_hint.trimmed();
  const z7::app::ArchiveSessionToken session_token = panel.archive.current_token;
  const ArchiveWritebackPlan writeback_plan =
      build_archive_writeback_plan_for_panel(active_panel_index_);
  const QSharedPointer<QTemporaryDir> temp_dir =
      create_archive_open_temporary_directory(caption);
  if (temp_dir == nullptr) {
    return;
  }

  start_archive_source_extract_task(
      QStringLiteral("%1: %2").arg(caption, archive_path),
      caption,
      archive_path,
      archive_type_hint,
      session_token,
      temp_dir->path(),
      OverwriteMode::kOverwrite,
      entries,
      [this,
       temp_dir,
       entries,
       archive_path,
       archive_type_hint,
       session_token,
       writeback_plan,
       command_line,
       caption](
          bool ok,
          int,
          int,
          const QString&,
          const z7::app::OperationOutcome&) {
        if (!ok) {
          return;
        }

        const QStringList targets =
            extracted_archive_entry_paths(temp_dir->path(), entries);
        const QVector<ArchiveTempFileSnapshot> snapshots =
            extracted_archive_entry_snapshots(temp_dir->path(), entries);
        if (targets.isEmpty()) {
          return;
        }

        QSharedPointer<ArchiveTempSession> session(new ArchiveTempSession);
        session->purpose = ArchiveTempSessionPurpose::kViewEdit;
        session->temp_dir = temp_dir;
        session->archive_path = archive_path;
        session->archive_display_source = writeback_plan.current_display_source();
        session->archive_type_hint = archive_type_hint;
        session->session_token = session_token;
        session->command_caption = caption;
        session->file_snapshots = snapshots;

        QProcess* process = nullptr;
        QString error_message;
        if (!run_external_command_with_targets(
                command_line,
                targets,
                temp_dir->path(),
                &error_message,
                true,
                &process)) {
          if (!error_message.trimmed().isEmpty()) {
            QMessageBox::warning(this, caption, error_message);
          }
          return;
        }
        session->process = process;
        retain_archive_temp_session(session);
        QObject::connect(
            process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, session](int, QProcess::ExitStatus) {
              on_archive_temp_session_process_finished(session);
            });
        if (process->state() == QProcess::NotRunning) {
          QTimer::singleShot(0, this, [this, session]() {
            on_archive_temp_session_process_finished(session);
          });
        }
      });
}

void MainWindow::run_view_or_edit_requested(bool use_editor_command) {
  const QString caption =
      z7::ui::runtime_support::strip_mnemonic(use_editor_command ? z7::ui::runtime_support::L(544) : z7::ui::runtime_support::L(543));
  const QString missing_command_message =
      use_editor_command
          ? QStringLiteral(
                "No editor command is configured. Configure Editor in Options before using Edit.")
          : QStringLiteral(
                "No viewer command is configured. Configure Viewer in Options before using View.");

  if (!use_editor_command && !in_archive_view()) {
    PanelController& panel = active_panel_controller();
    if (panel.model != nullptr) {
      bool calculated = false;
      for (const QModelIndex& row : panel.selected_real_item_rows()) {
        if (!row.isValid() || !panel.model->is_dir_for_row(row.row())) {
          continue;
        }
        const QString path = panel.model->path_for_row(row.row());
        const QFileInfo info(path);
        if (!info.exists() || !info.isDir()) {
          continue;
        }
        const FilesystemDirectoryStats stats =
            collect_filesystem_directory_stats(info.absoluteFilePath());
        calculated =
            panel.model->set_filesystem_directory_stats(info.absoluteFilePath(),
                                                        stats.size,
                                                        stats.folders,
                                                        stats.files) ||
            calculated;
      }
      if (calculated) {
        update_status();
        if (panel.ui.details_view != nullptr) {
          panel.ui.details_view->viewport()->update();
        }
        return;
      }
    }
  }

  z7::platform::qt::PortableSettings settings;
  const QString command_line =
      settings
          .value(QString::fromLatin1(
                     use_editor_command ? kSettingsFmEditor : kSettingsFmViewer),
                 QString())
          .toString()
          .trimmed();
  if (command_line.isEmpty()) {
    QMessageBox::information(this, caption, missing_command_message);
    return;
  }

  if (in_archive_view()) {
    run_archive_view_or_edit_with_command(command_line, caption);
    return;
  }

  const PanelController& panel = active_panel_controller();
  if (panel.focused_item_is_parent_link() || panel.focused_item_is_dir()) {
    return;
  }

  const QString focused_path = panel.focused_path();
  if (focused_path.trimmed().isEmpty()) {
    return;
  }

  const QFileInfo info(focused_path);
  if (!info.exists() || !info.isFile()) {
    return;
  }
  const QStringList targets{info.absoluteFilePath()};

  QString error_message;
  if (!run_external_command_with_targets(
          command_line, targets, current_directory(), &error_message)) {
    if (!error_message.trimmed().isEmpty()) {
      QMessageBox::warning(this, caption, error_message);
    }
  }
}

bool MainWindow::resolve_diff_targets(QString* path1, QString* path2) const {
  if (path1 == nullptr || path2 == nullptr) {
    return false;
  }
  path1->clear();
  path2->clear();

  if (in_archive_view()) {
    return false;
  }

  const QStringList active_selected = selected_filesystem_paths_including_parent_link_for_panel(active_panel_index_);
  if (active_selected.size() == 2) {
    *path1 = QFileInfo(active_selected.at(0)).absoluteFilePath();
    *path2 = QFileInfo(active_selected.at(1)).absoluteFilePath();
    return !path1->isEmpty() && !path2->isEmpty();
  }

  if (active_selected.size() != 1 ||
      !two_panels_visible_ ||
      in_archive_view_for_panel(1 - active_panel_index_)) {
    return false;
  }

  *path1 = QFileInfo(active_selected.front()).absoluteFilePath();
  const QStringList opposite_selected = selected_filesystem_paths_including_parent_link_for_panel(1 - active_panel_index_);
  if (opposite_selected.size() == 1) {
    *path2 = QFileInfo(opposite_selected.front()).absoluteFilePath();
    return !path1->isEmpty() && !path2->isEmpty();
  }

  QString relative_path =
      QDir(current_directory_for_panel(active_panel_index_)).relativeFilePath(*path1);
  const DirectoryListModel* active_model = panel_controller(active_panel_index_).model;
  const DirectoryListModel* opposite_model = panel_controller(1 - active_panel_index_).model;
  if (active_model != nullptr &&
      opposite_model != nullptr &&
      active_model->flat_view() &&
      !opposite_model->flat_view()) {
    relative_path = QFileInfo(*path1).fileName();
  }
  *path2 = QFileInfo(
      QDir(current_directory_for_panel(1 - active_panel_index_)).filePath(relative_path))
               .absoluteFilePath();
  return !path1->isEmpty() && !path2->isEmpty();
}

void MainWindow::on_view_requested() {
  run_view_or_edit_requested(false);
}

void MainWindow::on_edit_requested() {
  run_view_or_edit_requested(true);
}

void MainWindow::on_diff_requested() {
  z7::platform::qt::PortableSettings settings;
  const QString diff_command =
      settings.value(QString::fromLatin1(kSettingsFmDiff), QString()).toString().trimmed();
  if (diff_command.isEmpty()) {
    return;
  }

  QString path1;
  QString path2;
  if (!resolve_diff_targets(&path1, &path2)) {
    return;
  }

  QString error_message;
  if (!run_external_command_with_targets(diff_command,
                                         QStringList{path1, path2},
                                         current_directory_for_panel(active_panel_index_),
                                         &error_message)) {
    if (!error_message.trimmed().isEmpty()) {
      QMessageBox::warning(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(554)), error_message);
    }
  }
}

void MainWindow::on_compress_requested() {
  const QString caption =
      z7::ui::runtime_support::strip_mnemonic(
          z7::ui::runtime_support::L(7200));

  if (in_archive_view()) {
    if (!can_add_external_files_to_archive_preview(active_panel_index_)) {
      return;
    }

    const ArchiveWritebackPlan plan =
        build_archive_writeback_plan_for_panel(active_panel_index_);
    QString initial_dir = active_panel_controller().archive.origin_dir.trimmed();
    if (initial_dir.isEmpty()) {
      initial_dir = QFileInfo(plan.source_archive).absolutePath();
    }
    const ArchiveAddSourcesDialogResult dialog_result =
        show_archive_add_sources_dialog(
            this,
            caption,
            initial_dir,
            plan.current_virtual_dir());
    if (!dialog_result.accepted || dialog_result.selected_paths.isEmpty()) {
      return;
    }

    if (!start_add_external_files_to_archive_preview(
            active_panel_index_,
            dialog_result.selected_paths,
            plan.current_virtual_dir(),
            caption)) {
      QMessageBox::information(this, caption, z7::ui::runtime_support::L(3015));
    }
    return;
  }

  if (active_selected_rows_include_parent_link()) {
    return;
  }

  const QStringList inputs = active_panel_controller().selected_real_item_paths();
  if (inputs.isEmpty()) {
    QMessageBox::information(this, caption, z7::ui::runtime_support::L(3015));
    return;
  }
  run_sevenzip_add_to_archive();
}


}  // namespace z7::ui::filemanager
