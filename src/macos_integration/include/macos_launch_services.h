#pragma once

#include <QString>
#include <QStringList>

namespace z7::macos_integration::launch_services {

struct FileManagerInstanceLaunchRequest {
  QString program_path;
  QStringList arguments;
};

enum class FileManagerInstanceLaunchResult {
  kLaunched,
  kFallbackToDetached,
  kFailed,
};

FileManagerInstanceLaunchResult launch_new_filemanager_instance(
    const FileManagerInstanceLaunchRequest& request,
    QString* error_message = nullptr);

}  // namespace z7::macos_integration::launch_services
