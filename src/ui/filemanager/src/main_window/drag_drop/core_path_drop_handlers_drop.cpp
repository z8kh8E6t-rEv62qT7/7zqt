// src/ui/filemanager/src/main_window/drag_drop/core_path_drop_handlers_drop.cpp
// Role: Drop execution flow after drag command selection.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "drag_source_marker.h"
#include "drop_effect_feedback.h"
#include "drop_logic.h"

namespace z7::ui::filemanager {

namespace {

QString archive_drop_destination_virtual_dir(const QString& virtual_dir) {
  return z7::ui::archive_support::normalize_virtual_dir(virtual_dir);
}

QString archive_drop_destination_display_path(
    const QString& normalized_virtual_dir) {
  if (normalized_virtual_dir.isEmpty()) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1").arg(normalized_virtual_dir);
}

}  // namespace

bool MainWindow::handle_panel_drop(QObject* watched,
                                   int panel_index,
                                   bool window_drop_target,
                                   QDropEvent* drop_event) {
  if (drop_event == nullptr) {
    return false;
  }

  InternalArchiveSourcePayload archive_payload;
  bool archive_marker_trusted = false;
  const bool has_trusted_archive_source =
      read_internal_archive_source_marker(drop_event->mimeData(),
                                          &archive_payload,
                                          &archive_marker_trusted) &&
      archive_marker_trusted &&
      !archive_payload.entries.isEmpty();
  const QStringList dropped_paths = has_trusted_archive_source
                                        ? QStringList()
                                        : local_existing_drop_paths(
                                              drop_event->mimeData());
  if (dropped_paths.isEmpty() && !has_trusted_archive_source) {
    return false;
  }

  set_active_panel(panel_index);

  const bool archive_view = in_archive_view_for_panel(panel_index);
  const PanelController& panel = panel_controller(panel_index);
  QAbstractItemView* target_view = drop_target_view_for_panel(panel, watched);
  const QString panel_fs_directory =
      panel.model != nullptr ? panel.model->directory() : QString();
  const DropTargetInfo drop_target = resolve_drop_target_info_for_panel(
      panel_index, target_view, watched, drop_event, panel_fs_directory);
  const QString drop_target_directory = drop_target.directory;

  const DropSourceState source_state = resolve_drop_source_state(
      panel_index,
      window_drop_target,
      drop_event,
      dropped_paths,
      drop_target_directory);
  if (source_state.same_panel_source) {
    apply_windows_drop_effect_feedback(drop_event,
                                       false,
                                       Qt::IgnoreAction,
                                       false);
    drop_event->setDropAction(Qt::IgnoreAction);
    drop_event->ignore();
    return true;
  }

  const DropCommand command = choose_drop_command(
      this,
      archive_view,
      drop_target.allow_copy_move,
      window_drop_target,
      source_state.internal_fs_source,
      source_state.internal_archive_source,
      source_state.source_target_same_volume,
      watched,
      drop_event);

  const auto reject_drop = [drop_event]() {
    apply_windows_drop_effect_feedback(drop_event,
                                       false,
                                       Qt::IgnoreAction,
                                       false);
    drop_event->setDropAction(Qt::IgnoreAction);
    drop_event->ignore();
    return true;
  };

  bool started = false;
  switch (command) {
    case DropCommand::kAddToArchive: {
      QString add_target_directory = drop_target_directory;
      if (archive_view) {
        // Original PanelDrag::CompressDropFiles() passes empty folderPath for
        // archive-context AddToArc, which means "create archive near source".
        add_target_directory.clear();
      }
      const QString archive_name =
          z7::shell_integration::shell_integration_create_archive_name_from_paths(
              dropped_paths, false, nullptr);
      if (archive_name.trimmed().isEmpty()) {
        return reject_drop();
      }

      QString base_folder = add_target_directory.trimmed();
      if (base_folder.isEmpty()) {
        base_folder = QFileInfo(dropped_paths.front()).absolutePath();
      }
      if (!base_folder.isEmpty()) {
        base_folder = QDir(base_folder).absolutePath();
      }
      if (base_folder.trimmed().isEmpty()) {
        return reject_drop();
      }
      const QString archive_path = QDir(base_folder).filePath(archive_name);
      const QString caption = add_caption();
      z7::task_ipc_runtime::TaskIpcPayload payload;
      payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
      payload.show_dialog = true;
      payload.refresh_after_finish = true;
      payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
      payload.add->archive_path = archive_path;
      payload.add->input_paths = dropped_paths;
      started = launch_gui_subprocess_task(caption, payload);
      break;
    }
    case DropCommand::kCopyToArchive: {
      if (panel.archive.source_archive.trimmed().isEmpty()) {
        return reject_drop();
      }
      const QString archive_destination_virtual_dir =
          archive_drop_destination_virtual_dir(drop_target.archive_virtual_dir);

      QMessageBox::StandardButton confirm = QMessageBox::Yes;
#ifdef Z7_TESTING
      const DropCopyToArchiveConfirmOverride confirm_override =
          parse_copy_to_archive_confirm_override(
              property("z7.fm.drop.copy_to_archive.confirm.override"));
      if (confirm_override == DropCopyToArchiveConfirmOverride::kNo) {
        confirm = QMessageBox::No;
      } else if (confirm_override == DropCopyToArchiveConfirmOverride::kCancel) {
        confirm = QMessageBox::Cancel;
      } else if (confirm_override == DropCopyToArchiveConfirmOverride::kDialog) {
#endif
        const QString confirm_title = z7::ui::runtime_support::strip_mnemonic(lang_or(6010));
        const QString confirm_text = QStringLiteral("%1\n%2\n%3 ?")
                                         .arg(z7::ui::runtime_support::strip_mnemonic(lang_or(6002)),
                                              archive_drop_destination_display_path(
                                                  archive_destination_virtual_dir),
                                              z7::ui::runtime_support::strip_mnemonic(lang_or(6011)));
        confirm = QMessageBox::question(
            this,
            confirm_title,
            confirm_text,
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes);
#ifdef Z7_TESTING
      }
#endif
      if (confirm != QMessageBox::Yes) {
        return reject_drop();
      }

      started = start_add_external_files_to_archive_preview(
          panel_index,
          dropped_paths,
          archive_destination_virtual_dir,
          z7::ui::runtime_support::strip_mnemonic(lang_or(6002)));
      break;
    }
    case DropCommand::kCopy:
    case DropCommand::kMove: {
      if (drop_target_directory.trimmed().isEmpty()) {
        return reject_drop();
      }
      if (source_state.internal_archive_source) {
        if (source_state.archive_source_path.trimmed().isEmpty() ||
            source_state.archive_source_entries.isEmpty()) {
          return reject_drop();
        }
        started = start_archive_source_extract_task(
            QStringLiteral("%1: %2")
                .arg(z7::ui::runtime_support::strip_mnemonic(lang_or(6000)),
                     QDir::toNativeSeparators(drop_target_directory)),
            z7::ui::runtime_support::strip_mnemonic(lang_or(6000)),
            source_state.archive_source_path,
            source_state.archive_source_type_hint,
            source_state.archive_source_session_token,
            drop_target_directory,
            OverwriteMode::kOverwrite,
            source_state.archive_source_entries,
            [this](bool ok,
                   int,
                   int,
                   const QString&,
                   const z7::app::OperationOutcome&) {
              if (ok) {
                refresh_directory();
              }
            });
        break;
      }

      const bool move_mode = command == DropCommand::kMove;
      const QString op_caption =
          z7::ui::runtime_support::strip_mnemonic(lang_or(move_mode ? 6001 : 6000));
      const QString destination_display =
          QDir::toNativeSeparators(drop_target_directory);
      started = start_task_with_runner(
          QStringLiteral("%1: %2").arg(op_caption, destination_display),
          op_caption,
          [move_mode, dropped_paths, drop_target_directory](
              ArchiveProcessRunner* runner) {
            if (runner == nullptr) {
              return false;
            }
            return move_mode
                       ? runner->start_move_paths(
                             dropped_paths,
                             drop_target_directory,
                             OverwriteMode::kOverwrite)
                       : runner->start_copy_paths(
                             dropped_paths,
                             drop_target_directory,
                             OverwriteMode::kOverwrite);
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
      break;
    }
    case DropCommand::kCancel:
    default:
      return reject_drop();
  }

  if (started) {
    if (source_state.internal_archive_source) {
      if (QMimeData* mutable_mime_data =
              const_cast<QMimeData*>(drop_event->mimeData());
          mutable_mime_data != nullptr) {
        mutable_mime_data->setData(
            QString::fromLatin1(kMimeTypeZ7FmArchiveInternalDropHandled),
            QByteArrayLiteral("1"));
      }
    }
    const Qt::DropAction reported_action =
        reported_drop_action_for_source(command,
                                        source_state.trusted_internal_fs_source);
    apply_windows_drop_effect_feedback(drop_event,
                                       true,
                                       reported_action,
                                       source_state.trusted_internal_fs_source);
    drop_event->setDropAction(reported_action);
    drop_event->accept();
  } else {
    apply_windows_drop_effect_feedback(drop_event,
                                       false,
                                       Qt::IgnoreAction,
                                       false);
    drop_event->setDropAction(Qt::IgnoreAction);
    drop_event->ignore();
  }
  return true;
}

}  // namespace z7::ui::filemanager
