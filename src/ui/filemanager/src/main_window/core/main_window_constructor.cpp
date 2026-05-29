// src/ui/filemanager/src/main_window/core/main_window_constructor.cpp
// Role: MainWindow constructor and launcher default wiring.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

bool default_external_opener(const QString& path) {
  return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
  external_command_launcher_ =
      [](const QString& program,
         const QStringList& args,
         const QString& working_dir,
         qint64* pid) {
        return QProcess::startDetached(program, args, working_dir, pid);
      };
  external_opener_ = &default_external_opener;
  backend_capabilities_ = ArchiveProcessRunner::query_backend_capabilities();
  setup_ui();
  load_folder_history();
  restore_panel_paths_from_settings();
  restore_main_window_geometry();
  setup_actions();
  load_runtime_settings();
  setup_connections();
  retranslate_ui();
  apply_runtime_settings();
  restore_panel_ui_state_from_settings();
  z7::task_ipc_runtime::ensure_task_ipc_bootstrap_ready(nullptr);
}

MainWindow::~MainWindow() {
  dispatch_detached_archive_session_close(run_shutdown_cleanup_once());
  if (!task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    z7::task_ipc_runtime::clear_task_ipc_event_notifier(
        task_ipc_owner_instance_id_, nullptr);
  }
}

void MainWindow::open_startup_target(const QString& path,
                                     const QString& archive_type_hint) {
  const QString trimmed_path = path.trimmed();
  if (trimmed_path.isEmpty()) {
    return;
  }

  const QFileInfo info(trimmed_path);
  if (!info.exists()) {
    return;
  }

  if (info.isDir()) {
    set_current_directory(info.absoluteFilePath());
    return;
  }

  const QString target_file = info.absoluteFilePath();
  const QString trimmed_type_hint = archive_type_hint.trimmed();
  if (!trimmed_type_hint.isEmpty()) {
    open_archive_inside(target_file, trimmed_type_hint);
    return;
  }
  if (is_archive_file(target_file)) {
    open_archive_inside(target_file);
    return;
  }

  const QString parent_path = info.absolutePath();
  if (!parent_path.isEmpty()) {
    set_current_directory(parent_path);
  }
}

}  // namespace z7::ui::filemanager
