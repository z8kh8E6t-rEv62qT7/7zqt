#pragma once

#include <QSet>
#include <QString>

namespace z7::macos_integration {

struct MacOSIntegrationConfigSnapshot {
  bool enabled = true;
  bool visible_actions_configured = false;
  QSet<QString> visible_actions;
  bool cascaded_menu = true;
  bool show_menu_icons = false;
  QString locale_preferred;
};

MacOSIntegrationConfigSnapshot load_macos_integration_config_from_settings();
MacOSIntegrationConfigSnapshot load_macos_integration_config_snapshot(QString* error_message = nullptr);

bool save_macos_integration_config_snapshot(const MacOSIntegrationConfigSnapshot& config,
                          QString* error_message = nullptr);
bool sync_macos_integration_config_snapshot_from_settings(QString* error_message = nullptr);

QString macos_integration_snapshot_path();

}  // namespace z7::macos_integration
