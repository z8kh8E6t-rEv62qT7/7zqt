// src/ui/filemanager/src/main_window/core/main_window_constructor.cpp
// Role: MainWindow constructor and launcher default wiring.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    bool default_external_opener(QString const& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
        external_command_launcher_ =
            [](QString const& program, QStringList const& args, QString const& working_dir, qint64* pid) {
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
        close_archive_sessions_for_shutdown(run_shutdown_cleanup_once());
        if (!task_ipc_owner_instance_id_.trimmed().isEmpty()) {
            z7::task_ipc_runtime::clear_task_ipc_event_notifier(task_ipc_owner_instance_id_, nullptr);
        }
    }

    void MainWindow::open_startup_target(QString const& path, QString const& archive_type_hint) {
        StartupOpenTargetOptions options;
        options.archive_type_hint = archive_type_hint;
        open_startup_target(path, std::move(options));
    }

    void MainWindow::open_startup_target(QString const& path, StartupOpenTargetOptions options) {
        auto finish = [&options](bool ok) {
            if (options.finished_cb) {
                options.finished_cb(ok);
            }
        };

        QString const trimmed_path = path.trimmed();
        if (trimmed_path.isEmpty()) {
            finish(false);
            return;
        }

        QFileInfo const info(trimmed_path);
        if (!info.exists()) {
            finish(false);
            return;
        }

        if (info.isDir()) {
            set_current_directory(info.absoluteFilePath());
            finish(true);
            return;
        }

        QString const target_file = info.absoluteFilePath();
        auto open_archive_target = [&](QString const& archive_type_hint) {
            OpenArchiveInsideOptions archive_options;
            archive_options.archive_type_hint = archive_type_hint;
            archive_options.task_ui_mode =
                options.use_delayed_archive_progress ? RunnerTaskUiMode::kDelayed : RunnerTaskUiMode::kSilent;
            archive_options.finished_cb = options.finished_cb;
            if (options.fallback_to_parent_dir_on_archive_failure) {
                QString const parent_path = info.absolutePath();
                archive_options.open_failure_fallback = [this, parent_path]() {
                    if (!parent_path.isEmpty()) {
                        set_current_directory(parent_path);
                    }
                };
                archive_options.open_failure_fallback_policy = OpenFailureFallbackPolicy::kAnyFailure;
            }
            open_archive_inside(target_file, std::move(archive_options));
        };

        QString const trimmed_type_hint = options.archive_type_hint.trimmed();
        if (!trimmed_type_hint.isEmpty()) {
            open_archive_target(trimmed_type_hint);
            return;
        }
        if (is_archive_file(target_file)) {
            open_archive_target(QString());
            return;
        }

        QString const parent_path = info.absolutePath();
        if (!parent_path.isEmpty()) {
            set_current_directory(parent_path);
        }
        finish(true);
    }

} // namespace z7::ui::filemanager
