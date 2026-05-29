// src/ui/filemanager/src/main_window/open/open_commands_temp_session.cpp
// Role: Temporary extracted-file session lifecycle and archive update flow.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <memory>

#include <algorithm>

#include "archive_delegate_qt.h"
#include "common/archive_type_normalization.h"
#include "large_pages_settings.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace z7::ui::filemanager {
namespace {

QString normalize_archive_type_hint_token(const QString& value) {
  const QString lowered = value.trimmed().toLower();
  if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
    return QString();
  }
  return QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(value.toStdString()));
}

QString canonical_archive_type_from_suffix(const QString& value) {
  const QString lowered = value.trimmed().toLower();
  if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
    return QString();
  }
  return QString::fromStdString(
      z7::common::canonical_archive_type_from_filename_suffix_copy(
          value.toStdString()));
}

QString archive_parent_virtual_dir(const QString& entry) {
  const QString normalized = QDir::fromNativeSeparators(entry);
  const int slash_pos = normalized.lastIndexOf(QLatin1Char('/'));
  if (slash_pos <= 0) {
    return QString();
  }
  return normalized.left(slash_pos);
}

QString archive_temp_update_source_unavailable_message(
    const QString& extracted_path,
    const bool exists) {
  const QString display_path =
      QDir::toNativeSeparators(QDir::cleanPath(extracted_path));
  if (!exists) {
    return QStringLiteral(
               "Cannot update archive because the extracted file no longer "
               "exists:\n%1")
        .arg(display_path);
  }
  return QStringLiteral(
             "Cannot update archive because the extracted path is no longer a "
             "file:\n%1")
      .arg(display_path);
}

}  // namespace

MainWindow::ArchiveTempFileSnapshot MainWindow::capture_archive_temp_file_snapshot(
    const QString& archive_entry,
    const QString& extracted_path) const {
  ArchiveTempFileSnapshot snapshot;
  snapshot.archive_entry = z7::ui::archive_support::normalize_virtual_dir(archive_entry);
  snapshot.extracted_path =
      QDir::cleanPath(QDir::fromNativeSeparators(extracted_path));
  const QFileInfo info(snapshot.extracted_path);
  snapshot.existed = info.exists() && info.isFile();
  if (snapshot.existed) {
    snapshot.size = info.size();
    snapshot.mtime_msecs_utc = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
  }
  return snapshot;
}

bool MainWindow::start_archive_source_extract_task(
    const QString& task_header,
    const QString& failure_caption,
    const QString& archive_path,
    const QString& archive_type_hint,
    z7::app::ArchiveSessionToken session_token,
    const QString& output_dir,
    OverwriteMode overwrite_mode,
    const QStringList& archive_entries,
    const std::function<void(bool,
                             int,
                             int,
                             const QString&,
                             const z7::app::OperationOutcome&)>& finished_cb,
    RunnerTaskUiMode task_ui_mode,
    const std::function<bool(int, const QString&)>& should_show_failure) {
  return start_task_with_runner(
      task_header,
      failure_caption,
      [archive_path,
       archive_type_hint,
       session_token,
       output_dir,
       overwrite_mode,
       archive_entries](ArchiveProcessRunner* runner) {
        if (runner == nullptr) {
          return false;
        }
        if (session_token.is_valid()) {
          return runner->start_extract_in_session(
              session_token,
              output_dir,
              overwrite_mode,
              archive_entries);
        }
        return runner->start_extract_selected(
            archive_path,
            output_dir,
            overwrite_mode,
            archive_entries,
            archive_type_hint);
      },
      finished_cb,
      task_ui_mode,
      should_show_failure);
}

QStringList MainWindow::extracted_archive_entry_paths(
    const QString& temp_dir_path,
    const QStringList& archive_entries) const {
  QStringList extracted_paths;
  extracted_paths.reserve(archive_entries.size());
  for (const QString& entry : archive_entries) {
    const QString rel_path = QDir::fromNativeSeparators(entry);
    const QFileInfo extracted_info(QDir(temp_dir_path).filePath(rel_path));
    if (!extracted_info.exists()) {
      continue;
    }
    extracted_paths << extracted_info.absoluteFilePath();
  }
  extracted_paths.removeDuplicates();
  return extracted_paths;
}

QVector<MainWindow::ArchiveTempFileSnapshot>
MainWindow::extracted_archive_entry_snapshots(
    const QString& temp_dir_path,
    const QStringList& archive_entries) const {
  QVector<ArchiveTempFileSnapshot> snapshots;
  snapshots.reserve(archive_entries.size());
  for (const QString& entry : archive_entries) {
    const QString rel_path = QDir::fromNativeSeparators(entry);
    const QFileInfo extracted_info(QDir(temp_dir_path).filePath(rel_path));
    if (!extracted_info.exists()) {
      continue;
    }
    snapshots.push_back(
        capture_archive_temp_file_snapshot(
            entry,
            extracted_info.absoluteFilePath()));
  }
  return snapshots;
}

bool MainWindow::archive_temp_file_snapshot_changed(
    const ArchiveTempFileSnapshot& snapshot) const {
  const QFileInfo current(snapshot.extracted_path);
  const bool current_exists = current.exists() && current.isFile();
  if (current_exists != snapshot.existed) {
    return true;
  }
  if (!current_exists) {
    return false;
  }
  const qint64 current_mtime =
      current.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
  return current.size() != snapshot.size || current_mtime != snapshot.mtime_msecs_utc;
}

QString MainWindow::archive_update_format_for_session(
    const ArchiveTempSession& session) const {
  QString format = normalize_archive_type_hint_token(session.archive_type_hint);
  if (format.isEmpty()) {
    format = canonical_archive_type_from_suffix(
        QFileInfo(session.archive_display_source.isEmpty()
                      ? session.archive_path
                      : session.archive_display_source)
            .suffix());
  }
  return format;
}

void MainWindow::update_archive_entries_from_snapshots(
    const ArchiveTempSession& session,
    const QVector<ArchiveTempFileSnapshot>& changed_snapshots,
    const std::function<void(bool, const QString&)>& finished_cb) const {
  const auto finish = [finished_cb](const bool ok, const QString& message) {
    if (finished_cb) {
      finished_cb(ok, message);
    }
  };

  const QString archive_format =
      session.session_token.is_valid()
          ? QString()
          : archive_update_format_for_session(session);
  if (!session.session_token.is_valid() && archive_format.trimmed().isEmpty()) {
    finish(false, QStringLiteral("Cannot determine archive format for update."));
    return;
  }
  for (const ArchiveTempFileSnapshot& snapshot : changed_snapshots) {
    if (snapshot.archive_entry.trimmed().isEmpty() ||
        snapshot.extracted_path.trimmed().isEmpty()) {
      continue;
    }
    const QFileInfo source_info(snapshot.extracted_path);
    if (!source_info.exists() || !source_info.isFile()) {
      finish(false,
             archive_temp_update_source_unavailable_message(
                 snapshot.extracted_path,
                 source_info.exists()));
      return;
    }
  }

  struct SnapshotUpdateChain final
      : public std::enable_shared_from_this<SnapshotUpdateChain> {
    QPointer<const MainWindow> owner;
    std::string archive_format_utf8;
    z7::app::ArchiveSessionToken session_token;
    QVector<MainWindow::ArchiveTempFileSnapshot> snapshots;
    int cursor = 0;
    std::function<void(bool, const QString&)> finished;
    z7::app::ArchiveEngine engine;
    z7::app::ArchiveSession active_session;

    void start_next() {
      while (cursor < snapshots.size()) {
        const MainWindow::ArchiveTempFileSnapshot& snapshot = snapshots[cursor];
        if (snapshot.archive_entry.trimmed().isEmpty() ||
            snapshot.extracted_path.trimmed().isEmpty()) {
          ++cursor;
          continue;
        }
        const QFileInfo source_info(snapshot.extracted_path);
        if (!source_info.exists() || !source_info.isFile()) {
          complete(false,
                   archive_temp_update_source_unavailable_message(
                       snapshot.extracted_path,
                       source_info.exists()));
          return;
        }

        z7::app::AddRequest request;
        request.session_token = session_token;
        if (!archive_format_utf8.empty()) {
          request.format = archive_format_utf8;
        }
        request.update_mode = "update";
        request.directory = z7::ui::archive_support::to_utf8_string(
            archive_parent_virtual_dir(snapshot.archive_entry));
        request.input_paths.push_back(z7::ui::archive_support::to_native_string(source_info.absoluteFilePath()));

        const std::shared_ptr<SnapshotUpdateChain> self = shared_from_this();
        auto delegate = std::make_shared<z7::ui::archive_support::OutcomeRelayDelegate>(
            const_cast<MainWindow*>(owner.data()),
            [self](const z7::app::OperationOutcome& outcome) {
              self->on_single_update_finished(outcome);
            },
            nullptr,
            z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect);
        z7::ui::runtime_support::apply_configured_large_pages_mode();
        active_session = engine.start(z7::app::ArchiveRequest{std::move(request)}, delegate);
        if (!active_session.valid()) {
          const z7::app::OperationOutcome unavailable =
              z7::app::make_backend_unavailable_outcome();
          complete(false, z7::ui::archive_support::from_utf8_string(unavailable.summary));
        }
        return;
      }

      complete(true, QString());
    }

    void on_single_update_finished(const z7::app::OperationOutcome& outcome) {
      const auto result = z7::app::outcome_payload_as<z7::app::AddResult>(outcome);
      if (!result.has_value() || !result->ok) {
        QString error_message;
        if (result.has_value()) {
          error_message = z7::ui::archive_support::from_utf8_string(result->summary);
        }
        if (error_message.trimmed().isEmpty()) {
          error_message = z7::ui::archive_support::from_utf8_string(outcome.error.message);
        }
        if (error_message.trimmed().isEmpty()) {
          error_message = QStringLiteral("Failed to update modified archive entries.");
        }
        complete(false, error_message);
        return;
      }

      ++cursor;
      start_next();
    }

    void complete(const bool ok, const QString& message) {
      active_session = z7::app::ArchiveSession{};
      if (!owner.isNull() && finished) {
        finished(ok, message);
      }
    }
  };

  auto chain = std::make_shared<SnapshotUpdateChain>();
  chain->owner = this;
  chain->archive_format_utf8 = z7::ui::archive_support::to_utf8_string(archive_format);
  chain->session_token = session.session_token;
  chain->snapshots = changed_snapshots;
  chain->finished = finish;
  chain->start_next();
}

void MainWindow::retain_archive_temp_session(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session == nullptr || session->temp_dir == nullptr ||
      !session->temp_dir->isValid()) {
    return;
  }
  archive_temp_sessions_.erase(
      std::remove_if(
          archive_temp_sessions_.begin(),
          archive_temp_sessions_.end(),
          [](const QSharedPointer<ArchiveTempSession>& item) {
            return item == nullptr || item->temp_dir == nullptr ||
                   !item->temp_dir->isValid();
          }),
      archive_temp_sessions_.end());
  archive_temp_sessions_.push_back(session);
}

void MainWindow::release_archive_temp_session(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session != nullptr) {
    session->process_finished_handled = true;
    if (session->process != nullptr) {
      session->process->deleteLater();
      session->process = nullptr;
    }
    for (const QPointer<QProcess>& process : session->tracked_processes) {
      if (process != nullptr) {
        process->deleteLater();
      }
    }
    session->tracked_processes.clear();
  }
#if defined(Q_OS_WIN)
  if (session != nullptr) {
    for (void* raw_handle : session->tracked_process_handles) {
      if (raw_handle != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(raw_handle));
      }
    }
    session->tracked_process_handles.clear();
  }
#endif
  archive_temp_sessions_.erase(
      std::remove_if(
          archive_temp_sessions_.begin(),
          archive_temp_sessions_.end(),
          [&session](const QSharedPointer<ArchiveTempSession>& item) {
            return item == nullptr || item == session;
          }),
      archive_temp_sessions_.end());
}

void MainWindow::reload_matching_archive_writeback_panels(
    const QString& archive_path,
    const QString& archive_display_source,
    z7::app::ArchiveSessionToken session_token) {
  const int panel_order[2] = {active_panel_index_, 1 - active_panel_index_};
  for (const int panel_index : panel_order) {
    const PanelController& panel = panel_controller(panel_index);
    const bool matches_writeback_target =
        panel.matches_archive_writeback_target(archive_path,
                                              archive_display_source);
    const bool matches_session_token =
        session_token.is_valid() && panel.archive.current_token == session_token;
    if (!matches_writeback_target && !matches_session_token) {
      continue;
    }

    const QString panel_virtual_dir = panel.archive.virtual_dir;
    const QString panel_origin_dir = panel.archive.origin_dir;
    const QString panel_type_hint = panel.archive.type_hint;
    load_archive_virtual_directory_for_panel(panel_index,
                                             archive_path,
                                             panel_virtual_dir,
                                             panel_origin_dir,
                                             panel_type_hint,
                                             false,
                                             {},
                                             false,
                                             {},
                                             session_token,
                                             archive_display_source);
  }
}

void MainWindow::finalize_archive_temp_session(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session == nullptr) {
    return;
  }
  if (session->purpose == ArchiveTempSessionPurpose::kOpenOutside) {
    release_archive_temp_session(session);
    return;
  }
  if (!session->file_snapshots.isEmpty()) {
    on_archive_temp_session_process_finished(session);
    return;
  }
  release_archive_temp_session(session);
}

void MainWindow::on_open_outside_temp_session_tracking_finished(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session == nullptr || session->process_finished_handled) {
    return;
  }
  if (session->pending_open_outside_trackers > 0) {
    --session->pending_open_outside_trackers;
  }
  if (session->pending_open_outside_trackers == 0 &&
      session->open_outside_cleanup_policy ==
          OpenOutsideCleanupPolicy::kReleaseWhenTrackersDrain) {
    on_open_outside_temp_session_finished(session);
  }
}

void MainWindow::on_open_outside_temp_session_finished(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session == nullptr || session->process_finished_handled) {
    return;
  }
  release_archive_temp_session(session);
}

void MainWindow::on_archive_temp_session_process_finished(
    const QSharedPointer<ArchiveTempSession>& session) {
  if (session == nullptr || session->process_finished_handled) {
    return;
  }
  session->process_finished_handled = true;
  if (session->process != nullptr) {
    session->process->deleteLater();
    session->process = nullptr;
  }

  QVector<ArchiveTempFileSnapshot> changed_snapshots;
  changed_snapshots.reserve(session->file_snapshots.size());
  for (const ArchiveTempFileSnapshot& snapshot : session->file_snapshots) {
    if (archive_temp_file_snapshot_changed(snapshot)) {
      changed_snapshots.push_back(snapshot);
    }
  }
  if (changed_snapshots.isEmpty()) {
    release_archive_temp_session(session);
    return;
  }

  const QString prompt_title = session->command_caption.isEmpty()
                                   ? z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(544))
                                   : session->command_caption;
  const QString prompt_text = QStringLiteral(
      "The extracted file was modified.\n\nDo you want to update the archive?");
  const auto ask_question = [this](const QString& title,
                                   const QString& message,
                                   QMessageBox::StandardButtons buttons,
                                   QMessageBox::StandardButton default_button) {
    if (question_box_) {
      return question_box_(title, message, buttons, default_button);
    }
    return QMessageBox::question(this, title, message, buttons, default_button);
  };
  const QMessageBox::StandardButton answer = ask_question(
      prompt_title,
      prompt_text,
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
      QMessageBox::Yes);
  if (answer != QMessageBox::Yes) {
    release_archive_temp_session(session);
    return;
  }

  update_archive_entries_from_snapshots(
      *session,
      changed_snapshots,
      [this, session, prompt_title](const bool ok, const QString& update_error) {
        if (!ok) {
          release_archive_temp_session(session);
          QMessageBox::warning(this,
                               prompt_title,
                               update_error.trimmed().isEmpty()
                                   ? QStringLiteral("Failed to update archive.")
                                   : update_error);
          return;
        }

        const QString archive_path = session->archive_path;
        const QString archive_display_source = session->archive_display_source;
        const z7::app::ArchiveSessionToken session_token =
            session->session_token;
        release_archive_temp_session(session);
        reload_matching_archive_writeback_panels(archive_path,
                                                 archive_display_source,
                                                 session_token);
      });
}

}  // namespace z7::ui::filemanager
