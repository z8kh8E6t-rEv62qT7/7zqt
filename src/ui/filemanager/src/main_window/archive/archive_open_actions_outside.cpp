// src/ui/filemanager/src/main_window/archive/archive_open_actions_outside.cpp
// Role: Open archive entries outside without managed external writeback.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "archive_open_actions_tracking.h"

#include <limits>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace z7::ui::filemanager {
using namespace archive_open_tracking;

namespace {

QString build_open_outside_failure_message(const QStringList& failed_paths) {
  if (failed_paths.isEmpty()) {
    return z7::ui::runtime_support::J(
        QStringLiteral("ui.archive.open_outside.failed_launch"));
  }

  QStringList display_paths;
  display_paths.reserve(failed_paths.size());
  for (const QString& path : failed_paths) {
    display_paths << QDir::toNativeSeparators(path);
  }

  return z7::ui::runtime_support::JF(
      QStringLiteral("ui.archive.open_outside.failed_launch_for_paths"),
      {display_paths.join(QLatin1Char('\n'))});
}

}  // namespace

MainWindow::OpenOutsideLaunchResult MainWindow::launch_open_outside(
    const QStringList& paths,
    const QString& working_dir) {
  OpenOutsideLaunchResult result;
  if (paths.isEmpty()) {
    result.message = z7::ui::runtime_support::J(
        QStringLiteral("ui.archive.open_outside.no_external_paths"));
    return result;
  }

#ifdef Q_OS_MAC
  QProcess* open_process = new QProcess(this);
  QStringList open_args;
  open_args << QStringLiteral("-W");
  open_args << paths;
  open_process->setWorkingDirectory(working_dir);
  open_process->start(QStringLiteral("open"), open_args);
  if (open_process->waitForStarted(5000)) {
    result.launched_paths = paths;
    result.tracked_processes.push_back(open_process);
  } else {
    result.failed_paths = paths;
    open_process->deleteLater();
  }
#endif

#ifdef Q_OS_WIN
  for (const QString& path : paths) {
    HANDLE process_handle = nullptr;
    if (!open_path_externally_with_process_handle(path, &process_handle)) {
      result.failed_paths << path;
      continue;
    }
    result.launched_paths << path;
    if (process_handle != nullptr) {
      result.tracked_process_handles.push_back(process_handle);
    }
  }
#endif

#ifdef Q_OS_LINUX
  for (const QString& path : paths) {
    QProcess* open_process = new QProcess(this);
    open_process->setWorkingDirectory(working_dir);
    open_process->start(QStringLiteral("xdg-open"), QStringList{path});
    if (!open_process->waitForStarted(5000)) {
      result.failed_paths << path;
      open_process->deleteLater();
      continue;
    }
    result.tracked_processes.push_back(open_process);
    result.launched_paths << path;
  }
#endif

  if (!result.has_success()) {
    result.message = build_open_outside_failure_message(
        result.failed_paths.isEmpty() ? paths : result.failed_paths);
    return result;
  }
  if (result.has_failures()) {
    result.message = build_open_outside_failure_message(result.failed_paths);
  }
  return result;
}

void MainWindow::open_archive_entries_outside_for_panel(int panel_index,
                                                         const QStringList& entries) {
  if (entries.isEmpty()) {
    return;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (!in_archive_view_for_panel(panel_index) ||
      panel.archive.source_archive.isEmpty()) {
    return;
  }

  const QSharedPointer<QTemporaryDir> temp_dir =
      create_archive_open_temporary_directory(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540)));
  if (temp_dir == nullptr) {
    return;
  }

  const QString archive_path = panel.archive.source_archive;
  const QString archive_type_hint = panel.archive.type_hint.trimmed();
  const z7::app::ArchiveSessionToken session_token = panel.archive.current_token;
  const QString command_caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540));
  start_archive_source_extract_task(
      QStringLiteral("%1: %2").arg(command_caption, archive_path),
      command_caption,
      archive_path,
      archive_type_hint,
      session_token,
      temp_dir->path(),
      OverwriteMode::kOverwrite,
      entries,
      [this,
       temp_dir,
       entries,
       command_caption](
          bool ok,
          int,
          int,
          const QString&,
          const z7::app::OperationOutcome&) {
        if (!ok) {
          return;
        }

        const QStringList extracted_paths =
            extracted_archive_entry_paths(temp_dir->path(), entries);
        if (extracted_paths.isEmpty()) {
          return;
        }

        auto build_session = [&](const QStringList& launched_paths) {
          QSharedPointer<ArchiveTempSession> session(new ArchiveTempSession);
          session->purpose = ArchiveTempSessionPurpose::kOpenOutside;
          session->temp_dir = temp_dir;
          session->command_caption = command_caption;
          session->extracted_paths = launched_paths;
          return session;
        };

        const OpenOutsideLaunchResult launch_result =
            launch_open_outside(extracted_paths, temp_dir->path());
        if (!launch_result.has_success()) {
          QMessageBox::warning(
              this,
              command_caption,
              launch_result.message.trimmed().isEmpty()
                  ? z7::ui::runtime_support::J(
                        QStringLiteral("ui.archive.open_outside.failed_launch"))
                  : launch_result.message);
          return;
        }

        QSharedPointer<ArchiveTempSession> session =
            build_session(launch_result.launched_paths);
        session->tracked_processes = launch_result.tracked_processes;
#if defined(Q_OS_WIN)
        session->tracked_process_handles = launch_result.tracked_process_handles;
#endif
        session->pending_open_outside_trackers =
            session->tracked_processes.size()
#if defined(Q_OS_WIN)
            + session->tracked_process_handles.size()
#endif
            ;
        session->open_outside_cleanup_policy =
            launch_result.has_tracked_lifecycle()
                ? OpenOutsideCleanupPolicy::kReleaseWhenTrackersDrain
                : OpenOutsideCleanupPolicy::kRetainUntilClose;
        retain_archive_temp_session(session);
        for (const QPointer<QProcess>& process : launch_result.tracked_processes) {
          if (process == nullptr) {
            on_open_outside_temp_session_tracking_finished(session);
            continue;
          }
          QObject::connect(
              process,
              qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
              this,
              [this, session](int, QProcess::ExitStatus) {
                on_open_outside_temp_session_tracking_finished(session);
              });
          if (process->state() == QProcess::NotRunning) {
            QTimer::singleShot(
                0,
                this,
                [this, session]() {
                  on_open_outside_temp_session_tracking_finished(session);
                });
          }
        }
        if (session->pending_open_outside_trackers > 0) {
#if defined(Q_OS_WIN)
          if (!session->tracked_process_handles.isEmpty()) {
            auto poll_handles = std::make_shared<std::function<void()>>();
            *poll_handles = [this, session, poll_handles]() {
              if (session == nullptr || session->process_finished_handled) {
                return;
              }

              bool any_pending_handle = false;
              for (void*& raw_handle : session->tracked_process_handles) {
                if (raw_handle == nullptr) {
                  continue;
                }
                HANDLE const handle = static_cast<HANDLE>(raw_handle);
                const DWORD wait_result = ::WaitForSingleObject(handle, 0);
                if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) {
                  raw_handle = nullptr;
                  on_open_outside_temp_session_tracking_finished(session);
                  continue;
                }
                any_pending_handle = true;
              }

              if (any_pending_handle) {
                QTimer::singleShot(
                    200,
                    this,
                    [poll_handles]() { (*poll_handles)(); });
              }
            };
            QTimer::singleShot(
                200,
                this,
                [poll_handles]() { (*poll_handles)(); });
          }
#endif
        }
        if (launch_result.has_failures()) {
          QMessageBox::warning(
              this,
              command_caption,
              launch_result.message.trimmed().isEmpty()
                  ? z7::ui::runtime_support::J(
                        QStringLiteral("ui.archive.open_outside.failed_launch"))
                  : launch_result.message);
        }
      });
}

}  // namespace z7::ui::filemanager
