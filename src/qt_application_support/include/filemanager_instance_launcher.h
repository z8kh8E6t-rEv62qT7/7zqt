#pragma once

#include <QString>
#ifdef Z7_TESTING
#include <QStringList>
#include <functional>
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

    using LaunchOverride = std::function<bool(LaunchRequest const& request, QString* error_message)>;
#endif

    bool launch_open_request_for_program(QString const& program,
                                         QString const& target_path,
                                         QString const& archive_type_hint,
                                         QString const& working_dir,
                                         QString* error_message = nullptr);

    bool launch_open_request_for_current_app(QString const& target_path,
                                             QString const& archive_type_hint,
                                             QString const& working_dir,
                                             QString* error_message = nullptr);

#ifdef Z7_TESTING
    void set_launch_override_for_testing(LaunchOverride override);
    void reset_launch_override_for_testing();
#endif

} // namespace z7::platform::qt::filemanager_instance_launcher
