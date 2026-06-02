#include "macos_integration_config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtGlobal>

#include <pwd.h>
#include <unistd.h>

#include <cstdint>
#include <vector>

#include "shell_integration_menu.h"

namespace z7::macos_integration {
namespace {
namespace shell = z7::shell_integration;

inline constexpr const char* kSettingsOptionsIntegrateShell = "Options/IntegrateToShellMenu";
inline constexpr const char* kSettingsOptionsCascadedMenu = "Options/CascadedMenu";
inline constexpr const char* kSettingsOptionsMenuIcons = "Options/MenuIcons";
inline constexpr const char* kSettingsOptionsContextMenu = "Options/ContextMenu";
inline constexpr const char* kSettingsLanguage = "Lang";
inline constexpr const char* kSettingsFileName = "settings.json";
inline constexpr const char* kFileManagerAppName = "7zFM";
#ifdef Z7_TESTING
inline constexpr const char* kTestSettingsRootEnv = "Z7_TEST_PORTABLE_SETTINGS_ROOT";
#endif

QSet<QString> defaults_as_set() {
  const QStringList defaults = shell::default_shell_integration_visible_actions();
  return QSet<QString>(defaults.cbegin(), defaults.cend());
}

MacOSIntegrationConfig default_config() {
  MacOSIntegrationConfig config;
  config.enabled = true;
  config.visible_actions_configured = false;
  config.visible_actions = defaults_as_set();
  config.cascaded_menu = true;
  config.show_menu_icons = false;
  config.locale_preferred = QStringLiteral("en");
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  config.enabled = false;
#endif
  return config;
}

std::uint32_t context_menu_flags_from_json(const QJsonValue& value,
                                           bool* ok_out) {
  bool ok = false;
  qulonglong raw = 0;
  if (value.isDouble()) {
    const double as_double = value.toDouble();
    if (as_double >= 0) {
      raw = static_cast<qulonglong>(as_double);
      ok = static_cast<double>(raw) == as_double;
    }
  } else if (value.isString()) {
    raw = value.toString().trimmed().toULongLong(&ok);
  }
  if (ok_out != nullptr) {
    *ok_out = ok;
  }
  return ok ? static_cast<std::uint32_t>(raw) : 0;
}

bool is_sandbox_container_home(const QString& path) {
  return path.contains(QStringLiteral("/Library/Containers/")) ||
         path.contains(QStringLiteral("/Library/Group Containers/"));
}

QString real_user_home_dir_from(QString value) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty() || !trimmed.startsWith(QLatin1Char('/'))) {
    return QString();
  }
  const QString cleaned = QDir::cleanPath(trimmed);
  return is_sandbox_container_home(cleaned) ? QString() : cleaned;
}

QString posix_user_home_dir() {
  long buffer_size = 16384;
#ifdef _SC_GETPW_R_SIZE_MAX
  const long configured_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (configured_size > 0 && configured_size < 1024 * 1024) {
    buffer_size = configured_size;
  }
#endif

  std::vector<char> buffer(static_cast<std::size_t>(buffer_size));
  passwd pwd = {};
  passwd* result = nullptr;
  if (getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result) != 0 ||
      result == nullptr || pwd.pw_dir == nullptr) {
    return QString();
  }
  return QString::fromLocal8Bit(pwd.pw_dir);
}

QString read_only_settings_root_dir() {
#ifdef Z7_TESTING
  const QString test_root = qEnvironmentVariable(kTestSettingsRootEnv).trimmed();
  if (!test_root.isEmpty()) {
    return test_root;
  }
#endif

  const QString posix_home = real_user_home_dir_from(posix_user_home_dir());
  if (!posix_home.isEmpty()) {
    return QDir(posix_home).filePath(QStringLiteral(".config/7zqt"));
  }

  const QString environment_home =
      real_user_home_dir_from(qEnvironmentVariable("HOME"));
  if (!environment_home.isEmpty()) {
    return QDir(environment_home).filePath(QStringLiteral(".config/7zqt"));
  }

  return QString();
}

QJsonObject read_7zfm_settings_object() {
  const QString settings_root = read_only_settings_root_dir();
  if (settings_root.isEmpty()) {
    return QJsonObject{};
  }

  QFile file(QDir(settings_root).filePath(QString::fromLatin1(kSettingsFileName)));
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return QJsonObject{};
  }

  QJsonParseError parse_error;
  const QJsonDocument doc =
      QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    return QJsonObject{};
  }

  return doc.object()
      .value(QStringLiteral("apps"))
      .toObject()
      .value(QString::fromLatin1(kFileManagerAppName))
      .toObject();
}

QString preferred_locale_from_settings(const QJsonObject& settings) {
  const QString stored =
      settings.value(QString::fromLatin1(kSettingsLanguage)).toString().trimmed();
  if (!stored.isEmpty() && stored != QStringLiteral("-")) {
    return stored;
  }
  return QStringLiteral("en");
}

}  // namespace

MacOSIntegrationConfig load_macos_integration_config_from_settings() {
  MacOSIntegrationConfig config = default_config();
  const QJsonObject settings = read_7zfm_settings_object();
  if (settings.isEmpty()) {
    return config;
  }

  config.enabled =
      settings.value(QString::fromLatin1(kSettingsOptionsIntegrateShell))
          .toBool(config.enabled);
  config.cascaded_menu =
      settings.value(QString::fromLatin1(kSettingsOptionsCascadedMenu))
          .toBool(config.cascaded_menu);
  config.show_menu_icons =
      settings.value(QString::fromLatin1(kSettingsOptionsMenuIcons))
          .toBool(config.show_menu_icons);

  const QString context_menu_key =
      QString::fromLatin1(kSettingsOptionsContextMenu);
  if (settings.contains(context_menu_key)) {
    bool ok = false;
    const std::uint32_t flags =
        context_menu_flags_from_json(settings.value(context_menu_key), &ok);
    if (ok) {
      const QStringList normalized =
          shell::shell_integration_visible_actions_from_context_menu_flags(
              flags);
      config.visible_actions_configured = true;
      config.visible_actions =
          QSet<QString>(normalized.cbegin(), normalized.cend());
    }
  }

  config.locale_preferred = preferred_locale_from_settings(settings);
  return config;
}

}  // namespace z7::macos_integration
