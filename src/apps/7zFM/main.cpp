#include <QMessageBox>
#include <QSet>
#include <QTimer>

#include "app_startup_qt.h"
#include "filemanager_instance_launcher.h"
#include "file_open_support.h"
#include "main_window.h"
#include "official_lang_catalog.h"
#include "portable_settings.h"

namespace {

QString startup_request_key(const z7::apps::filemanager::OpenRequest& request) {
  QString key = request.path.trimmed();
  key.append(QChar(0));
  key.append(request.type_hint.trimmed());
  return key;
}

QVector<z7::apps::filemanager::OpenRequest> take_undispatched_startup_requests(
    z7::apps::filemanager::FileOpenApplication* app,
    QSet<QString>* dispatched_request_keys) {
  if (app == nullptr || dispatched_request_keys == nullptr) {
    return {};
  }

  QVector<z7::apps::filemanager::OpenRequest> undispatched_requests;
  const QVector<z7::apps::filemanager::OpenRequest> pending_requests =
      app->take_pending_open_requests();
  undispatched_requests.reserve(pending_requests.size());
  for (const z7::apps::filemanager::OpenRequest& request : pending_requests) {
    const QString key = startup_request_key(request);
    if (dispatched_request_keys->contains(key)) {
      continue;
    }
    dispatched_request_keys->insert(key);
    undispatched_requests.push_back(request);
  }
  return undispatched_requests;
}

void open_request_in_new_window(
    const z7::apps::filemanager::OpenRequest& request) {
  auto* window = new z7::ui::filemanager::MainWindow();
  window->open_startup_target(request.path, request.type_hint);
  window->show();
}

void launch_open_request_in_new_process(
    const z7::apps::filemanager::OpenRequest& request) {
  QString error_message;
  z7::platform::qt::filemanager_instance_launcher::launch_open_request_for_current_app(
      request.path, request.type_hint, QString(), &error_message);
}

}  // namespace

int main(int argc, char* argv[])
{
    Q_INIT_RESOURCE(generated_filemanager_resources);

    z7::platform::qt::AppStartupConfig const startup = z7::platform::qt::startup_config_with_persisted_overrides(
        z7::platform::qt::StartupAppKind::kFileManager, argc > 0 ? QString::fromLocal8Bit(argv[0]) : QString());
    z7::platform::qt::apply_pre_app_startup(startup);
    z7::apps::filemanager::FileOpenApplication app(argc, argv);
    z7::platform::qt::apply_post_app_startup(app, startup);

    QString lang_error;
    if (!z7::ui::runtime_support::OfficialLangCatalog::validate_required_language_resources(&lang_error))
    {
        QMessageBox::critical(nullptr,
                              QStringLiteral("7zFM"),
                              QStringLiteral("Cannot initialize language resources:\n%1").arg(lang_error));
        return 2;
    }

    QString settings_error;
    if (!z7::platform::qt::initialize_portable_settings(&settings_error))
    {
        QMessageBox::critical(nullptr,
                              QStringLiteral("7zFM"),
                              QStringLiteral("Cannot initialize portable config at \"%1\":\n%2")
                                  .arg(z7::platform::qt::portable_settings_root_dir(), settings_error));
        return 1;
    }

    const z7::apps::filemanager::StartupOpenArgumentParseResult startup_open_args =
        z7::apps::filemanager::parse_startup_open_arguments(app.arguments());
    app.enqueue_open_requests(startup_open_args.requests);

    QSet<QString> dispatched_startup_request_keys;
    z7::ui::filemanager::MainWindow* primary_window = nullptr;
    bool primary_window_received_startup_request = false;
    auto open_in_primary_window =
        [&](const z7::apps::filemanager::OpenRequest& request)
    {
        if (primary_window_received_startup_request)
        {
            open_request_in_new_window(request);
            return;
        }
        if (primary_window == nullptr)
        {
            primary_window = new z7::ui::filemanager::MainWindow();
        }
        primary_window->open_startup_target(request.path, request.type_hint);
        primary_window->show();
        primary_window_received_startup_request = true;
    };

    auto dispatch_undispatched_startup_requests =
        [&]() -> bool
    {
        const QVector<z7::apps::filemanager::OpenRequest> startup_requests =
            take_undispatched_startup_requests(
                &app, &dispatched_startup_request_keys);
        if (startup_requests.isEmpty())
        {
            return false;
        }
        z7::apps::filemanager::dispatch_startup_open_requests(
            startup_requests,
            open_in_primary_window,
            open_request_in_new_window);
        app.remember_dispatched_startup_open_requests(startup_requests);
        return true;
    };

    while (dispatch_undispatched_startup_requests()) {}

#if defined(Q_OS_MACOS)
    constexpr int kStartupFileOpenGraceMs = 1000;

    auto install_runtime_open_request_handler =
        [&app]()
    {
        app.set_open_request_handler(launch_open_request_in_new_process);
    };

    if (primary_window != nullptr)
    {
        install_runtime_open_request_handler();
    }
    else
    {
        bool startup_file_open_handoff_finished = false;
        QString startup_file_open_handoff_key;
        auto finish_startup_file_open_handoff =
            [&primary_window,
             &dispatch_undispatched_startup_requests,
             &install_runtime_open_request_handler,
             &startup_file_open_handoff_finished]()
        {
            if (startup_file_open_handoff_finished)
            {
                return;
            }
            startup_file_open_handoff_finished = true;
            while (dispatch_undispatched_startup_requests()) {}
            if (primary_window == nullptr)
            {
                primary_window = new z7::ui::filemanager::MainWindow();
                primary_window->show();
            }
            install_runtime_open_request_handler();
        };

        app.set_open_request_handler(
            [&app,
             &install_runtime_open_request_handler,
             &open_in_primary_window,
             &startup_file_open_handoff_key,
             &startup_file_open_handoff_finished](
                const z7::apps::filemanager::OpenRequest& request)
            {
                if (startup_file_open_handoff_finished)
                {
                    if (startup_request_key(request) ==
                        startup_file_open_handoff_key)
                    {
                        return;
                    }
                    launch_open_request_in_new_process(request);
                    return;
                }

                startup_file_open_handoff_finished = true;
                startup_file_open_handoff_key = startup_request_key(request);
                app.remember_dispatched_startup_open_requests(
                    QVector<z7::apps::filemanager::OpenRequest>{request});
                open_in_primary_window(request);
                QTimer::singleShot(
                    0,
                    &app,
                    install_runtime_open_request_handler);
            });

        QTimer::singleShot(
            kStartupFileOpenGraceMs,
            &app,
            finish_startup_file_open_handoff);
    }
#else
    if (primary_window == nullptr)
    {
        primary_window = new z7::ui::filemanager::MainWindow();
        primary_window->show();
    }
    app.set_open_request_handler(launch_open_request_in_new_process);
#endif

    return app.exec();
}
