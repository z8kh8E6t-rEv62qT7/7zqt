#include "filemanager_instance_launcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#if defined(Q_OS_MACOS)
#include "macos_launch_services.h"
#endif

#include "json_localization.h"

#include <utility>

namespace z7::platform::qt::filemanager_instance_launcher {
namespace {

#ifndef Z7_TESTING
struct LaunchRequest {
  QString program;
  QString path;
  QString type_hint;
  QString working_dir;
  QStringList arguments;
};
#endif

#ifdef Z7_TESTING
LaunchOverride& launch_override() {
  static LaunchOverride override;
  return override;
}
#endif

QString normalized_program_path(const QString& program,
                                QString* error_message) {
  const QString trimmed = program.trimmed();
  if (trimmed.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot locate 7zFM executable path.");
    }
    return QString();
  }

  const QFileInfo info(trimmed);
  if (!info.exists() || !info.isFile()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot locate 7zFM executable path: %1")
                           .arg(trimmed);
    }
    return QString();
  }
  return info.absoluteFilePath();
}

QString normalized_target_path(const QString& target_path,
                               QString* error_message) {
  const QString trimmed = QDir::fromNativeSeparators(target_path.trimmed());
  if (trimmed.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          z7::i18n::text(
              QStringLiteral("ui.navigation.task_ipc.open_requires_one_target"));
    }
    return QString();
  }

  const QFileInfo info(trimmed);
  if (!info.exists()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Open target does not exist: %1").arg(trimmed);
    }
    return QString();
  }
  return info.absoluteFilePath();
}

QString resolved_working_dir(const QString& working_dir,
                             const QString& normalized_target_path) {
  const QString trimmed = QDir::fromNativeSeparators(working_dir.trimmed());
  if (!trimmed.isEmpty()) {
    return trimmed;
  }

  const QFileInfo target_info(normalized_target_path);
  if (target_info.isDir()) {
    return target_info.absoluteFilePath();
  }
  const QString parent_dir = target_info.absolutePath();
  return parent_dir.isEmpty() ? QDir::currentPath() : parent_dir;
}

QStringList open_arguments(const QString& normalized_target_path,
                           const QString& archive_type_hint) {
  QStringList arguments;
  const QString type_hint = archive_type_hint.trimmed();
  if (!type_hint.isEmpty()) {
    arguments << QStringLiteral("-t%1").arg(type_hint);
  }
  arguments << normalized_target_path;
  return arguments;
}

bool launch_request_with_detached_process(const LaunchRequest& request,
                                          QString* error_message) {
  if (QProcess::startDetached(
          request.program, request.arguments, request.working_dir, nullptr)) {
    return true;
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral("Failed to launch detached 7zFM process.");
  }
  return false;
}

}  // namespace

bool launch_open_request_for_program(const QString& program,
                                     const QString& target_path,
                                     const QString& archive_type_hint,
                                     const QString& working_dir,
                                     QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  const QString normalized_program =
      normalized_program_path(program, error_message);
  if (normalized_program.isEmpty()) {
    return false;
  }
  const QString normalized_path = normalized_target_path(target_path, error_message);
  if (normalized_path.isEmpty()) {
    return false;
  }

  LaunchRequest request;
  request.program = normalized_program;
  request.path = normalized_path;
  request.type_hint = archive_type_hint.trimmed();
  request.working_dir = resolved_working_dir(working_dir, normalized_path);
  request.arguments = open_arguments(normalized_path, request.type_hint);

#ifdef Z7_TESTING
  if (launch_override()) {
    return launch_override()(request, error_message);
  }
#endif

#if defined(Q_OS_MACOS)
  const z7::macos_integration::launch_services::FileManagerInstanceLaunchResult
      macos_launch_result =
          z7::macos_integration::launch_services::launch_new_filemanager_instance(
              {request.program, request.arguments}, error_message);
  if (macos_launch_result ==
      z7::macos_integration::launch_services::FileManagerInstanceLaunchResult::
          kLaunched) {
    return true;
  }
  if (macos_launch_result ==
      z7::macos_integration::launch_services::FileManagerInstanceLaunchResult::
          kFailed) {
    return false;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
#endif

  return launch_request_with_detached_process(request, error_message);
}

bool launch_open_request_for_current_app(const QString& target_path,
                                         const QString& archive_type_hint,
                                         const QString& working_dir,
                                         QString* error_message) {
  return launch_open_request_for_program(QCoreApplication::applicationFilePath(),
                                         target_path,
                                         archive_type_hint,
                                         working_dir,
                                         error_message);
}

#ifdef Z7_TESTING
void set_launch_override_for_testing(LaunchOverride override) {
  launch_override() = std::move(override);
}

void reset_launch_override_for_testing() {
  launch_override() = {};
}
#endif

}  // namespace z7::platform::qt::filemanager_instance_launcher
