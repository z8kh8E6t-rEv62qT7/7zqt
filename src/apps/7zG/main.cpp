#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>

#include "app_startup_qt.h"
#include "portable_settings.h"
#include "official_lang_catalog.h"
#include "task_ipc_runtime.h"
#include "gui_app_controller.h"
#include "cli_bridge.h"

namespace {

int validate_runtime_environment() {
  QString lang_error;
  if (!z7::ui::runtime_support::OfficialLangCatalog::validate_required_language_resources(
          &lang_error)) {
    QMessageBox::critical(
        nullptr,
        QStringLiteral("7zG"),
        QStringLiteral("Cannot initialize language resources:\n%1").arg(lang_error));
    return 2;
  }

  QString settings_error;
  if (!z7::platform::qt::initialize_portable_settings(&settings_error)) {
    QMessageBox::critical(
        nullptr,
        QStringLiteral("7zG"),
        QStringLiteral("Cannot initialize portable config at \"%1\":\n%2")
            .arg(z7::platform::qt::portable_settings_root_dir(), settings_error));
    return 1;
  }
  return 0;
}

bool parse_task_ipc_identity(const QStringList& args,
                             quint64* out_session_id,
                             quint32* out_generation,
                             QString* out_shm_name,
                             QString* out_sem_name) {
  if (out_session_id != nullptr) {
    *out_session_id = 0;
  }
  if (out_generation != nullptr) {
    *out_generation = 0;
  }
  if (out_shm_name != nullptr) {
    out_shm_name->clear();
  }
  if (out_sem_name != nullptr) {
    out_sem_name->clear();
  }

  for (const QString& arg : args) {
    if (arg.startsWith(QStringLiteral("--task-ipc-session="))) {
      bool ok = false;
      const quint64 parsed =
          arg.mid(QStringLiteral("--task-ipc-session=").size()).toULongLong(&ok);
      if (!ok) {
        return false;
      }
      if (out_session_id != nullptr) {
        *out_session_id = parsed;
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-generation="))) {
      bool ok = false;
      const quint32 parsed =
          arg.mid(QStringLiteral("--task-ipc-generation=").size()).toUInt(&ok);
      if (!ok) {
        return false;
      }
      if (out_generation != nullptr) {
        *out_generation = parsed;
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-shm="))) {
      if (out_shm_name != nullptr) {
        *out_shm_name =
            arg.mid(QStringLiteral("--task-ipc-shm=").size()).trimmed();
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-sem="))) {
      if (out_sem_name != nullptr) {
        *out_sem_name =
            arg.mid(QStringLiteral("--task-ipc-sem=").size()).trimmed();
      }
    }
  }

  const bool has_identity = out_session_id != nullptr &&
                            *out_session_id != 0 &&
                            out_generation != nullptr &&
                            *out_generation != 0U;
#if defined(Q_OS_MACOS)
  return has_identity && out_shm_name != nullptr && !out_shm_name->isEmpty() &&
         out_sem_name != nullptr && !out_sem_name->isEmpty();
#else
  Q_UNUSED(out_shm_name);
  Q_UNUSED(out_sem_name);
  return has_identity;
#endif
}

bool contains_task_ipc_identity_arg(const QStringList& args) {
  for (const QString& arg : args) {
    if (arg.startsWith(QStringLiteral("--task-ipc-"))) {
      return true;
    }
  }
  return false;
}

bool publish_task_ipc_completion_with_fallback(
    const z7::task_ipc_runtime::TaskIpcClaimedTask& task,
    int result_code,
    const QString& summary) {
  QString task_ipc_error;
  if (z7::task_ipc_runtime::publish_task_ipc_completion(
          task, result_code, summary, &task_ipc_error)) {
    return true;
  }
  return z7::task_ipc_runtime::publish_task_ipc_completion_minimal(
      task, result_code, nullptr);
}

}  // namespace

int main(int argc, char* argv[]) {
  Q_INIT_RESOURCE(generated_filemanager_resources);

  QStringList raw_user_args;
  raw_user_args.reserve(argc > 1 ? argc - 1 : 0);
  for (int i = 1; i < argc; ++i) {
    raw_user_args.append(QString::fromLocal8Bit(argv[i]));
  }

  if (!contains_task_ipc_identity_arg(raw_user_args)) {
    const z7::platform::qt::AppStartupConfig startup =
        z7::platform::qt::startup_config_with_persisted_overrides(
            z7::platform::qt::StartupAppKind::kGui,
            argc > 0 ? QString::fromLocal8Bit(argv[0]) : QString());
    z7::platform::qt::apply_pre_app_startup(startup);
    QApplication app(argc, argv);
    z7::platform::qt::apply_post_app_startup(app, startup);

    const int runtime_status = validate_runtime_environment();
    if (runtime_status != 0) {
      return runtime_status;
    }
    return z7::apps::gui::run_cli_launcher(app.arguments().mid(1));
  }

  const z7::platform::qt::AppStartupConfig startup =
      z7::platform::qt::startup_config_with_persisted_overrides(
          z7::platform::qt::StartupAppKind::kGui,
          argc > 0 ? QString::fromLocal8Bit(argv[0]) : QString());
  z7::platform::qt::apply_pre_app_startup(startup);
  QApplication app(argc, argv);
  z7::platform::qt::apply_post_app_startup(app, startup);

  const int runtime_status = validate_runtime_environment();
  if (runtime_status != 0) {
    return runtime_status;
  }

  quint64 session_id = 0;
  quint32 generation = 0;
  QString shm_name;
  QString sem_name;
  const QStringList user_args = app.arguments().mid(1);
  if (!parse_task_ipc_identity(user_args, &session_id, &generation,
                               &shm_name, &sem_name)) {
    return 7;
  }
#if defined(Q_OS_MACOS)
  z7::task_ipc_runtime::set_task_ipc_worker_endpoint(shm_name, sem_name);
#endif

  const z7::ui::gui::SharedTaskCancellation task_cancellation =
      z7::ui::gui::TaskCancellation::create();
  z7::ui::gui::GuiAppController controller;
  int exit_code = 255;
  const auto finalize_exit = [&app, &exit_code](int result) {
    exit_code = result;
    app.exit(result);
  };

  // Defer claim/start until the main event loop is running so closing a modal
  // setup dialog cannot terminate the worker before the task begins.
  QTimer::singleShot(
      0,
      &app,
      [&controller,
       &finalize_exit,
       session_id,
       generation,
       task_cancellation]() {
        z7::task_ipc_runtime::TaskIpcClaimedTask task;
        QString claim_error;
        if (!z7::task_ipc_runtime::claim_task_ipc_task_for_worker(
                session_id, generation, &task, &claim_error)) {
          const QString detail = claim_error.trimmed().isEmpty()
                                     ? QStringLiteral("7zG failed to claim task IPC task.")
                                     : claim_error.trimmed();
          const bool likely_no_task =
              detail.contains(QStringLiteral("Requested task IPC task is no longer available"),
                              Qt::CaseInsensitive);
          if (!likely_no_task && !QGuiApplication::screens().isEmpty()) {
            QMessageBox::critical(nullptr, QStringLiteral("7zG"), detail);
          }
          finalize_exit(7);
          return;
        }

        const bool complete_on_claim = task.payload.complete_on_claim;
        const bool completion_published_on_claim =
            complete_on_claim &&
            publish_task_ipc_completion_with_fallback(task, 0, QString());
        QString cancel_notifier_error;
        z7::task_ipc_runtime::set_task_ipc_cancel_notifier(
            task,
            [task_cancellation]() {
              if (task_cancellation) {
                task_cancellation->request_cancel();
              }
            },
            &cancel_notifier_error);
        if (task_cancellation->is_canceled()) {
          publish_task_ipc_completion_with_fallback(
              task,
              5,
              QStringLiteral("Operation canceled."));
          finalize_exit(5);
          return;
        }

        if (task.payload.command ==
            z7::task_ipc_runtime::TaskIpcCommandKind::kCli) {
          z7::apps::gui::CliWorkerResult worker_result;
          worker_result.exit_code = 7;
          QString summary;
          if (!task.payload.cli.has_value()) {
            summary = QStringLiteral("7zG CLI task payload is missing.");
          } else {
            worker_result =
                z7::apps::gui::run_cli_worker_payload(*task.payload.cli);
            summary = worker_result.summary;
          }
          if (worker_result.exit_code != 0 && summary.isEmpty() &&
              !worker_result.error_dialog_shown) {
            summary = QStringLiteral("7zG CLI task failed (exit code %1).")
                          .arg(worker_result.exit_code);
          }
          if (!(complete_on_claim && completion_published_on_claim)) {
            publish_task_ipc_completion_with_fallback(
                task, worker_result.exit_code, summary);
          }
          finalize_exit(worker_result.exit_code);
          return;
        }

        controller.run_task_ipc_payload_async(
            task.payload,
            task_cancellation,
            [&finalize_exit,
             task,
             complete_on_claim,
             completion_published_on_claim](
                const z7::ui::gui::GuiTaskCompletion& completion) {
              QString summary = completion.summary.trimmed();
              if (summary.isEmpty() && completion.exit_code != 0) {
                summary = QStringLiteral("7zG task failed (exit code %1).")
                              .arg(completion.exit_code);
              }
              if (!(complete_on_claim && completion_published_on_claim)) {
                publish_task_ipc_completion_with_fallback(
                    task, completion.exit_code, summary);
              }
              finalize_exit(completion.exit_code);
            });
      });

  app.exec();
  return exit_code;
}
