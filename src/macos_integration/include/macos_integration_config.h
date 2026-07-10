#pragma once

#include <QSet>
#include <QString>

namespace z7::macos_integration {

    struct MacOSIntegrationConfig {
        bool enabled = true;
        bool visible_actions_configured = false;
        QSet<QString> visible_actions;
        bool cascaded_menu = true;
        bool show_menu_icons = false;
        QString locale_preferred;
    };

    MacOSIntegrationConfig load_macos_integration_config_from_settings();

} // namespace z7::macos_integration
