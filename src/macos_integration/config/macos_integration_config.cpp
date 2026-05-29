#include "macos_integration_config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cstdint>

#include "portable_settings.h"
#include "shell_integration_menu.h"

namespace z7::macos_integration {
namespace {
namespace shell = z7::shell_integration;

inline constexpr const char* kSettingsOptionsIntegrateShell = "Options/IntegrateToShellMenu";
inline constexpr const char* kSettingsOptionsCascadedMenu = "Options/CascadedMenu";
inline constexpr const char* kSettingsOptionsMenuIcons = "Options/MenuIcons";
inline constexpr const char* kSettingsOptionsContextMenu = "Options/ContextMenu";
inline constexpr const char* kSettingsLanguage = "Lang";
inline constexpr const char* kFileManagerAppName = "7zFM";

QSet<QString> defaults_as_set() {
  const QStringList defaults = shell::default_shell_integration_visible_actions();
  return QSet<QString>(defaults.cbegin(), defaults.cend());
}

QJsonArray visible_actions_to_json(const QSet<QString>& visible_actions) {
  QJsonArray array;
  for (const QString& action : shell::shell_integration_context_menu_action_ids()) {
    if (visible_actions.contains(action)) {
      array.push_back(action);
    }
  }
  return array;
}

QSet<QString> visible_actions_from_json(const QJsonValue& value) {
  const QJsonArray array = value.toArray();
  QStringList stored;
  stored.reserve(array.size());
  for (const QJsonValue& item : array) {
    stored.push_back(item.toString());
  }
  const QStringList normalized =
      shell::normalize_shell_integration_visible_actions(stored);
  QSet<QString> out;
  out.reserve(normalized.size());
  for (const QString& action : normalized) {
    out.insert(action);
  }
  return out;
}

std::uint32_t context_menu_flags_from_variant(const QVariant& value) {
  bool ok = false;
  const qulonglong raw = value.toULongLong(&ok);
  return ok ? static_cast<std::uint32_t>(raw) : 0;
}

z7::platform::qt::PortableSettings file_manager_settings() {
  return z7::platform::qt::PortableSettings(
      QString(), QString::fromLatin1(kFileManagerAppName));
}

QString preferred_locale_from_settings() {
  z7::platform::qt::PortableSettings settings = file_manager_settings();
  const QString stored =
      settings.value(QString::fromLatin1(kSettingsLanguage)).toString().trimmed();
  if (!stored.isEmpty() && stored != QStringLiteral("-")) {
    return stored;
  }
  return QStringLiteral("en");
}

}  // namespace

MacOSIntegrationConfigSnapshot load_macos_integration_config_from_settings() {
  z7::platform::qt::PortableSettings settings = file_manager_settings();

  MacOSIntegrationConfigSnapshot config;
  config.enabled = settings.value(QString::fromLatin1(kSettingsOptionsIntegrateShell), true).toBool();
  config.cascaded_menu = settings.value(QString::fromLatin1(kSettingsOptionsCascadedMenu), true).toBool();
  config.show_menu_icons = settings.value(QString::fromLatin1(kSettingsOptionsMenuIcons), false).toBool();
  const QString context_menu_key =
      QString::fromLatin1(kSettingsOptionsContextMenu);
  config.visible_actions_configured = settings.contains(context_menu_key);
  if (config.visible_actions_configured) {
    const QStringList normalized =
        shell::shell_integration_visible_actions_from_context_menu_flags(
            context_menu_flags_from_variant(settings.value(context_menu_key)));
    config.visible_actions = QSet<QString>(normalized.cbegin(), normalized.cend());
  } else {
    config.visible_actions = defaults_as_set();
  }
  config.locale_preferred = preferred_locale_from_settings();

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  config.enabled = false;
#endif

  return config;
}

MacOSIntegrationConfigSnapshot load_macos_integration_config_snapshot(QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  MacOSIntegrationConfigSnapshot fallback = load_macos_integration_config_from_settings();

  QFile file(macos_integration_snapshot_path());
  if (!file.open(QIODevice::ReadOnly)) {
    return fallback;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Finder extension snapshot is not a JSON object.");
    }
    return fallback;
  }

  const QJsonObject root = doc.object();
  MacOSIntegrationConfigSnapshot config;
  config.enabled = root.value(QStringLiteral("enabled")).toBool(fallback.enabled);
  config.cascaded_menu = root.value(QStringLiteral("cascaded_menu")).toBool(fallback.cascaded_menu);
  config.show_menu_icons = root.value(QStringLiteral("show_menu_icons")).toBool(fallback.show_menu_icons);
  config.locale_preferred = root.value(QStringLiteral("locale_preferred")).toString(fallback.locale_preferred).trimmed();
  config.visible_actions_configured = root.contains(QStringLiteral("visible_actions"))
                                          ? true
                                          : fallback.visible_actions_configured;
  if (root.contains(QStringLiteral("visible_actions"))) {
    config.visible_actions =
        visible_actions_from_json(root.value(QStringLiteral("visible_actions")));
  } else {
    config.visible_actions = fallback.visible_actions;
  }
  if (config.locale_preferred.isEmpty()) {
    config.locale_preferred = fallback.locale_preferred;
  }

  return config;
}

bool save_macos_integration_config_snapshot(const MacOSIntegrationConfigSnapshot& config,
                          QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  const QString snapshot_path = macos_integration_snapshot_path();
  const QFileInfo snapshot_info(snapshot_path);
  const QString parent_path = snapshot_info.absolutePath();
  if (!QDir().mkpath(parent_path)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot create finder snapshot directory: %1").arg(parent_path);
    }
    return false;
  }

  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("enabled"), config.enabled);
  if (config.visible_actions_configured) {
    root.insert(QStringLiteral("visible_actions"),
                visible_actions_to_json(config.visible_actions));
  }
  root.insert(QStringLiteral("cascaded_menu"), config.cascaded_menu);
  root.insert(QStringLiteral("show_menu_icons"), config.show_menu_icons);
  root.insert(QStringLiteral("locale_preferred"), config.locale_preferred.trimmed());

  QSaveFile output(snapshot_path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot open finder snapshot for writing: %1")
                           .arg(output.errorString());
    }
    return false;
  }

  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (output.write(bytes) != bytes.size()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot write finder snapshot: %1")
                           .arg(output.errorString());
    }
    output.cancelWriting();
    return false;
  }

  if (!output.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot commit finder snapshot: %1")
                           .arg(output.errorString());
    }
    return false;
  }

  return true;
}

bool sync_macos_integration_config_snapshot_from_settings(QString* error_message) {
  return save_macos_integration_config_snapshot(load_macos_integration_config_from_settings(), error_message);
}

QString macos_integration_snapshot_path() {
  return QDir(z7::platform::qt::portable_settings_root_dir())
      .filePath(QStringLiteral("macos_integration.json"));
}

}  // namespace z7::macos_integration
