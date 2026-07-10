// src/ui/filemanager/src/main_window/open/open_commands_temp_session.cpp
// Role: Temporary extracted-file session lifecycle and archive update flow.

#include <algorithm>
#include <memory>

#include "common/archive_type_normalization.h"
#include "main_window/deps.h"
#include "main_window/internal.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace z7::ui::filemanager {
    namespace {

        QString normalize_archive_type_hint_token(QString const& value) {
            QString const lowered = value.trimmed().toLower();
            if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
                return QString();
            }
            return QString::fromStdString(z7::common::normalize_archive_type_token_copy(value.toStdString()));
        }

        QString canonical_archive_type_from_suffix(QString const& value) {
            QString const lowered = value.trimmed().toLower();
            if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
                return QString();
            }
            return QString::fromStdString(
                z7::common::canonical_archive_type_from_filename_suffix_copy(value.toStdString()));
        }

        QString archive_temp_update_source_unavailable_message(QString const& extracted_path, bool const exists) {
            QString const display_path = QDir::toNativeSeparators(QDir::cleanPath(extracted_path));
            if (!exists) {
                return QStringLiteral("Cannot update archive because the extracted file no longer "
                                      "exists:\n%1")
                    .arg(display_path);
            }
            return QStringLiteral("Cannot update archive because the extracted path is no longer a "
                                  "file:\n%1")
                .arg(display_path);
        }

    } // namespace

    MainWindow::ArchiveTempFileSnapshot
    MainWindow::capture_archive_temp_file_snapshot(QString const& archive_entry, QString const& extracted_path) const {
        ArchiveTempFileSnapshot snapshot;
        snapshot.archive_entry = z7::ui::archive_support::normalize_virtual_dir(archive_entry);
        snapshot.extracted_path = QDir::cleanPath(QDir::fromNativeSeparators(extracted_path));
        QFileInfo const info(snapshot.extracted_path);
        snapshot.existed = info.exists() && info.isFile();
        if (snapshot.existed) {
            snapshot.size = info.size();
            snapshot.mtime_msecs_utc = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        }
        return snapshot;
    }

    bool MainWindow::start_archive_source_extract_task(
        QString const& task_header,
        QString const& failure_caption,
        QString const& archive_path,
        QString const& archive_type_hint,
        z7::app::ArchiveSessionToken session_token,
        QString const& output_dir,
        OverwriteMode overwrite_mode,
        QStringList const& archive_entries,
        std::function<void(bool, int, int, QString const&, z7::app::OperationOutcome const&)> const& finished_cb,
        RunnerTaskUiMode task_ui_mode,
        std::function<bool(int, QString const&)> const& should_show_failure) {
        return start_task_with_runner(
            task_header,
            failure_caption,
            [archive_path, archive_type_hint, session_token, output_dir, overwrite_mode, archive_entries](
                ArchiveProcessRunner* runner) {
                if (runner == nullptr) {
                    return false;
                }
                if (session_token.is_valid()) {
                    return runner->start_extract_in_session(session_token, output_dir, overwrite_mode, archive_entries);
                }
                return runner->start_extract_selected(
                    archive_path, output_dir, overwrite_mode, archive_entries, archive_type_hint);
            },
            finished_cb,
            task_ui_mode,
            should_show_failure);
    }

    QStringList MainWindow::extracted_archive_entry_paths(QString const& temp_dir_path,
                                                          QStringList const& archive_entries) const {
        QStringList extracted_paths;
        extracted_paths.reserve(archive_entries.size());
        for (QString const& entry : archive_entries) {
            QString const rel_path = QDir::fromNativeSeparators(entry);
            QFileInfo const extracted_info(QDir(temp_dir_path).filePath(rel_path));
            if (!extracted_info.exists()) {
                continue;
            }
            extracted_paths << extracted_info.absoluteFilePath();
        }
        extracted_paths.removeDuplicates();
        return extracted_paths;
    }

    QVector<MainWindow::ArchiveTempFileSnapshot>
    MainWindow::extracted_archive_entry_snapshots(QString const& temp_dir_path,
                                                  QStringList const& archive_entries) const {
        QVector<ArchiveTempFileSnapshot> snapshots;
        snapshots.reserve(archive_entries.size());
        for (QString const& entry : archive_entries) {
            QString const rel_path = QDir::fromNativeSeparators(entry);
            QFileInfo const extracted_info(QDir(temp_dir_path).filePath(rel_path));
            if (!extracted_info.exists()) {
                continue;
            }
            snapshots.push_back(capture_archive_temp_file_snapshot(entry, extracted_info.absoluteFilePath()));
        }
        return snapshots;
    }

    bool MainWindow::archive_temp_file_snapshot_changed(ArchiveTempFileSnapshot const& snapshot) const {
        QFileInfo const current(snapshot.extracted_path);
        bool const current_exists = current.exists() && current.isFile();
        if (current_exists != snapshot.existed) {
            return true;
        }
        if (!current_exists) {
            return false;
        }
        qint64 const current_mtime = current.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        return current.size() != snapshot.size || current_mtime != snapshot.mtime_msecs_utc;
    }

    QString MainWindow::archive_update_format_for_session(ArchiveTempSession const& session) const {
        QString format = normalize_archive_type_hint_token(session.archive_type_hint);
        if (format.isEmpty()) {
            format = canonical_archive_type_from_suffix(QFileInfo(session.archive_display_source.isEmpty()
                                                                      ? session.archive_path
                                                                      : session.archive_display_source)
                                                            .suffix());
        }
        return format;
    }

    void
    MainWindow::update_archive_entries_from_snapshots(ArchiveTempSession const& session,
                                                      QVector<ArchiveTempFileSnapshot> const& changed_snapshots,
                                                      std::function<void(bool, QString const&)> const& finished_cb) {
        auto const finish = [finished_cb](bool const ok, QString const& message) {
            if (finished_cb) {
                finished_cb(ok, message);
            }
        };

        QString const archive_format =
            session.session_token.is_valid() ? QString() : archive_update_format_for_session(session);
        if (!session.session_token.is_valid() && archive_format.trimmed().isEmpty()) {
            finish(false, QStringLiteral("Cannot determine archive format for update."));
            return;
        }
        for (ArchiveTempFileSnapshot const& snapshot : changed_snapshots) {
            if (snapshot.archive_entry.trimmed().isEmpty() || snapshot.extracted_path.trimmed().isEmpty()) {
                continue;
            }
            QFileInfo const source_info(snapshot.extracted_path);
            if (!source_info.exists() || !source_info.isFile()) {
                finish(false,
                       archive_temp_update_source_unavailable_message(snapshot.extracted_path, source_info.exists()));
                return;
            }
        }

        AddTaskOptions options;
        options.archive_path = session.archive_path;
        options.format = archive_format;
        options.session_token = session.session_token;
        options.update_mode = QStringLiteral("update");
        options.input_items.reserve(changed_snapshots.size());
        for (ArchiveTempFileSnapshot const& snapshot : changed_snapshots) {
            QString const entry = z7::ui::archive_support::normalize_virtual_dir(snapshot.archive_entry);
            if (entry.isEmpty() || snapshot.extracted_path.trimmed().isEmpty()) {
                continue;
            }
            ArchiveAddInputItem item;
            item.filesystem_path = QFileInfo(snapshot.extracted_path).absoluteFilePath();
            item.archive_entry = entry;
            options.input_items.push_back(std::move(item));
        }
        if (options.input_items.isEmpty()) {
            finish(true, QString());
            return;
        }

        QString const caption = archive_writeback_copying_caption();
        bool const started = start_task_with_runner(
            caption,
            caption,
            [options](ArchiveProcessRunner* runner) {
                return runner != nullptr && runner->start_add_to_archive(options);
            },
            [finish](bool ok, int, int, QString const&, z7::app::OperationOutcome const&) { finish(ok, QString()); });
        if (!started) {
            finish(false, QStringLiteral("Failed to start archive update."));
        }
    }

    void MainWindow::retain_archive_temp_session(QSharedPointer<ArchiveTempSession> const& session) {
        if (session == nullptr || session->temp_dir == nullptr || !session->temp_dir->isValid()) {
            return;
        }
        archive_temp_sessions_.erase(std::remove_if(archive_temp_sessions_.begin(),
                                                    archive_temp_sessions_.end(),
                                                    [](QSharedPointer<ArchiveTempSession> const& item) {
                                                        return item == nullptr
                                                            || item->temp_dir == nullptr
                                                            || !item->temp_dir->isValid();
                                                    }),
                                     archive_temp_sessions_.end());
        archive_temp_sessions_.push_back(session);
    }

    void MainWindow::release_archive_temp_session(QSharedPointer<ArchiveTempSession> const& session) {
        if (session != nullptr) {
            session->process_finished_handled = true;
            if (session->process != nullptr) {
                session->process->deleteLater();
                session->process = nullptr;
            }
            for (QPointer<QProcess> const& process : session->tracked_processes) {
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
        archive_temp_sessions_.erase(std::remove_if(archive_temp_sessions_.begin(),
                                                    archive_temp_sessions_.end(),
                                                    [&session](QSharedPointer<ArchiveTempSession> const& item) {
                                                        return item == nullptr || item == session;
                                                    }),
                                     archive_temp_sessions_.end());
    }

    void MainWindow::reload_matching_archive_writeback_panels(QString const& archive_path,
                                                              QString const& archive_display_source,
                                                              z7::app::ArchiveSessionToken session_token) {
        int const panel_order[2] = {active_panel_index_, 1 - active_panel_index_};
        for (int const panel_index : panel_order) {
            PanelController const& panel = panel_controller(panel_index);
            bool const matches_writeback_target =
                panel.matches_archive_writeback_target(archive_path, archive_display_source);
            bool const matches_session_token = session_token.is_valid() && panel.archive.current_token == session_token;
            if (!matches_writeback_target && !matches_session_token) {
                continue;
            }

            QString const panel_virtual_dir = panel.archive.virtual_dir;
            QString const panel_origin_dir = panel.archive.origin_dir;
            QString const panel_type_hint = panel.archive.type_hint;
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

    void MainWindow::finalize_archive_temp_session(QSharedPointer<ArchiveTempSession> const& session) {
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

    void MainWindow::on_open_outside_temp_session_tracking_finished(QSharedPointer<ArchiveTempSession> const& session) {
        if (session == nullptr || session->process_finished_handled) {
            return;
        }
        if (session->pending_open_outside_trackers > 0) {
            --session->pending_open_outside_trackers;
        }
        if (session->pending_open_outside_trackers == 0
            && session->open_outside_cleanup_policy == OpenOutsideCleanupPolicy::kReleaseWhenTrackersDrain) {
            on_open_outside_temp_session_finished(session);
        }
    }

    void MainWindow::on_open_outside_temp_session_finished(QSharedPointer<ArchiveTempSession> const& session) {
        if (session == nullptr || session->process_finished_handled) {
            return;
        }
        release_archive_temp_session(session);
    }

    void MainWindow::on_archive_temp_session_process_finished(QSharedPointer<ArchiveTempSession> const& session) {
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
        for (ArchiveTempFileSnapshot const& snapshot : session->file_snapshots) {
            if (archive_temp_file_snapshot_changed(snapshot)) {
                changed_snapshots.push_back(snapshot);
            }
        }
        if (changed_snapshots.isEmpty()) {
            release_archive_temp_session(session);
            return;
        }

        QString const prompt_title = session->command_caption.isEmpty()
                                       ? z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(544))
                                       : session->command_caption;
        QString const prompt_text =
            QStringLiteral("The extracted file was modified.\n\nDo you want to update the archive?");
        auto const ask_question = [this](QString const& title,
                                         QString const& message,
                                         QMessageBox::StandardButtons buttons,
                                         QMessageBox::StandardButton default_button) {
            if (question_box_) {
                return question_box_(title, message, buttons, default_button);
            }
            return QMessageBox::question(this, title, message, buttons, default_button);
        };
        QMessageBox::StandardButton const answer = ask_question(
            prompt_title, prompt_text, QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            release_archive_temp_session(session);
            return;
        }

        update_archive_entries_from_snapshots(
            *session, changed_snapshots, [this, session, prompt_title](bool const ok, QString const& update_error) {
                if (!ok) {
                    release_archive_temp_session(session);
                    if (!update_error.trimmed().isEmpty()) {
                        QMessageBox::warning(this, prompt_title, update_error);
                    }
                    return;
                }

                QString const archive_path = session->archive_path;
                QString const archive_display_source = session->archive_display_source;
                z7::app::ArchiveSessionToken const session_token = session->session_token;
                release_archive_temp_session(session);
                reload_matching_archive_writeback_panels(archive_path, archive_display_source, session_token);
            });
    }

} // namespace z7::ui::filemanager
