#pragma once

#include <QString>
#ifdef Z7_TESTING
#include <functional>
#include <QStringList>
#endif

namespace z7::platform::qt::filemanager_instance_launcher {

#ifdef Z7_TESTING
struct LaunchRequest {
  QString program;
  QString path;
  QString type_hint;
  QString working_dir;
  QStringList arguments;
};

using LaunchOverride =
    std::function<bool(const LaunchRequest& request, QString* error_message)>;
#endif

bool launch_open_request_for_program(const QString& program,
                                     const QString& target_path,
                                     const QString& archive_type_hint,
                                     const QString& working_dir,
                                     QString* error_message = nullptr);

bool launch_open_request_for_current_app(const QString& target_path,
                                         const QString& archive_type_hint,
                                         const QString& working_dir,
                                         QString* error_message = nullptr);

#ifdef Z7_TESTING
void set_launch_override_for_testing(LaunchOverride override);
void reset_launch_override_for_testing();
#endif

}  // namespace z7::platform::qt::filemanager_instance_launcher
