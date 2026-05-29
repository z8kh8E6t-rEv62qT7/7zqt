#include "app_startup_qt.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "portable_settings.h"
#include "portable_settings_internal.h"

namespace z7::platform::qt {

namespace {

constexpr int kBaseSmallIconExtent = 16;
constexpr int kBaseLargeToolbarIconExtent = 24;
constexpr int kBaseLargeListIconExtent = 48;
constexpr int kBaseDialogButtonMinWidth = 96;
constexpr int kBaseDialogButtonMinHeight = 30;
constexpr const char* kSharedSettingsAppName = "7z-shared";
constexpr const char* kStartupPreferredStyleKey = "Qt/Startup/PreferredStyle";
constexpr const char* kStartupHiDpiRoundingKey = "Qt/Startup/HiDpiRoundingPolicy";

QStyle* effective_style(const QWidget* reference) {
  if (reference != nullptr && reference->style() != nullptr) {
    return reference->style();
  }
  if (QApplication::instance() != nullptr) {
    return QApplication::style();
  }
  return nullptr;
}

int effective_font_height(const QWidget* reference) {
  if (reference != nullptr) {
    return reference->fontMetrics().height();
  }
  if (QApplication::instance() != nullptr) {
    return static_cast<int>(std::ceil(QFontMetricsF(QApplication::font()).height()));
  }
  return 14;
}

int style_metric(QStyle::PixelMetric metric, const QWidget* reference, int default_value) {
  QStyle* style = effective_style(reference);
  if (style == nullptr) {
    return default_value;
  }
  const int value = style->pixelMetric(metric, nullptr, reference);
  if (value <= 0) {
    return default_value;
  }
  return value;
}

QString resolve_style_name(const QString& requested) {
  if (requested.trimmed().isEmpty()) {
    return QString();
  }
  const QStringList keys = QStyleFactory::keys();
  for (const QString& key : keys) {
    if (QString::compare(key, requested, Qt::CaseInsensitive) == 0) {
      return key;
    }
  }
  return QString();
}

bool apply_style_if_available(QApplication& app, const QString& style_name) {
  const QString resolved = resolve_style_name(style_name);
  if (resolved.isEmpty()) {
    return false;
  }
  QStyle* style = QStyleFactory::create(resolved);
  if (style == nullptr) {
    return false;
  }
  app.setStyle(style);
  return true;
}

QStringList sorted_style_keys() {
  QStringList keys = QStyleFactory::keys();
  std::sort(keys.begin(), keys.end(), [](const QString& lhs, const QString& rhs) {
    return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
  });
  return keys;
}

QString startup_settings_path_from_argv0_hint(const QString& argv0_hint) {
  const QString trimmed_hint = argv0_hint.trimmed();
  if (trimmed_hint.isEmpty()) {
    return QString();
  }

  const QFileInfo exe_info(trimmed_hint);
  const QString absolute_exe_path = exe_info.isAbsolute()
                                        ? exe_info.absoluteFilePath()
                                        : QFileInfo(QDir::current(), trimmed_hint).absoluteFilePath();
  if (absolute_exe_path.isEmpty()) {
    return QString();
  }

  const QFileInfo absolute_info(absolute_exe_path);
  if (!absolute_info.exists() || !absolute_info.isFile()) {
    return QString();
  }

  const QString root_dir =
      portable_settings_internal::default_portable_settings_root_for_executable_hint(
          absolute_exe_path);
  if (root_dir.trimmed().isEmpty()) {
    return QString();
  }
  return QDir(root_dir).filePath(QStringLiteral("settings.json"));
}

QJsonObject read_shared_settings_json(const QString& settings_file_path) {
  if (settings_file_path.trimmed().isEmpty()) {
    return QJsonObject{};
  }

  QFile file(settings_file_path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return QJsonObject{};
  }

  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return QJsonObject{};
  }

  return document.object().value(QStringLiteral("shared")).toObject();
}

Qt::HighDpiScaleFactorRoundingPolicy parse_rounding_policy(const QString& raw_value,
                                                           bool* ok = nullptr) {
  const QString value = raw_value.trimmed().toLower();
  if (value == QStringLiteral("round")) {
    if (ok != nullptr) {
      *ok = true;
    }
    return Qt::HighDpiScaleFactorRoundingPolicy::Round;
  }
  if (value == QStringLiteral("ceil")) {
    if (ok != nullptr) {
      *ok = true;
    }
    return Qt::HighDpiScaleFactorRoundingPolicy::Ceil;
  }
  if (value == QStringLiteral("floor")) {
    if (ok != nullptr) {
      *ok = true;
    }
    return Qt::HighDpiScaleFactorRoundingPolicy::Floor;
  }
  if (value == QStringLiteral("round_prefer_floor") ||
      value == QStringLiteral("roundpreferfloor")) {
    if (ok != nullptr) {
      *ok = true;
    }
    return Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor;
  }
  if (value == QStringLiteral("pass_through") ||
      value == QStringLiteral("passthrough")) {
    if (ok != nullptr) {
      *ok = true;
    }
    return Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
  }
  if (ok != nullptr) {
    *ok = false;
  }
  return Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
}

QString rounding_policy_to_string(Qt::HighDpiScaleFactorRoundingPolicy policy) {
  switch (policy) {
    case Qt::HighDpiScaleFactorRoundingPolicy::Round:
      return QStringLiteral("round");
    case Qt::HighDpiScaleFactorRoundingPolicy::Ceil:
      return QStringLiteral("ceil");
    case Qt::HighDpiScaleFactorRoundingPolicy::Floor:
      return QStringLiteral("floor");
    case Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor:
      return QStringLiteral("round_prefer_floor");
    case Qt::HighDpiScaleFactorRoundingPolicy::PassThrough:
    case Qt::HighDpiScaleFactorRoundingPolicy::Unset:
    default:
      return QStringLiteral("pass_through");
  }
}

void apply_startup_overrides_from_shared_json(const QJsonObject& shared,
                                              AppStartupConfig* config) {
  if (config == nullptr) {
    return;
  }

  if (shared.contains(QString::fromLatin1(kStartupPreferredStyleKey))) {
    config->preferred_style =
        shared.value(QString::fromLatin1(kStartupPreferredStyleKey)).toString().trimmed();
  }
  if (shared.contains(QString::fromLatin1(kStartupHiDpiRoundingKey))) {
    bool ok = false;
    const QString policy_value =
        shared.value(QString::fromLatin1(kStartupHiDpiRoundingKey)).toString();
    const Qt::HighDpiScaleFactorRoundingPolicy policy =
        parse_rounding_policy(policy_value, &ok);
    if (ok) {
      config->hidpi.scale_factor_rounding = policy;
    }
  }
}

QString startup_organization_name_or_default(const QString& configured) {
  const QString trimmed = configured.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("7z2600") : trimmed;
}

}  // namespace

AppStartupConfig default_startup_config(StartupAppKind app_kind) {
  AppStartupConfig config;
  config.application_name =
      (app_kind == StartupAppKind::kFileManager) ? QStringLiteral("7zFM")
                                                 : QStringLiteral("7zFM");
  return config;
}

AppStartupConfig startup_config_with_persisted_overrides(StartupAppKind app_kind,
                                                         const QString& argv0_hint) {
  AppStartupConfig config = default_startup_config(app_kind);

  if (QCoreApplication::instance() != nullptr) {
    const PortableSettings shared(startup_organization_name_or_default(config.organization_name),
                                  QString::fromLatin1(kSharedSettingsAppName));
    if (shared.contains(QString::fromLatin1(kStartupPreferredStyleKey))) {
      config.preferred_style =
          shared.value(QString::fromLatin1(kStartupPreferredStyleKey)).toString().trimmed();
    }
    if (shared.contains(QString::fromLatin1(kStartupHiDpiRoundingKey))) {
      bool ok = false;
      const Qt::HighDpiScaleFactorRoundingPolicy policy = parse_rounding_policy(
          shared.value(QString::fromLatin1(kStartupHiDpiRoundingKey)).toString(), &ok);
      if (ok) {
        config.hidpi.scale_factor_rounding = policy;
      }
    }
    return config;
  }

  const QString settings_path = startup_settings_path_from_argv0_hint(argv0_hint);
  apply_startup_overrides_from_shared_json(read_shared_settings_json(settings_path), &config);
  return config;
}

void persist_startup_overrides(const AppStartupConfig& config) {
  PortableSettings shared(startup_organization_name_or_default(config.organization_name),
                          QString::fromLatin1(kSharedSettingsAppName));
  shared.setValue(QString::fromLatin1(kStartupPreferredStyleKey),
                  config.preferred_style.trimmed());
  shared.setValue(QString::fromLatin1(kStartupHiDpiRoundingKey),
                  rounding_policy_to_string(config.hidpi.scale_factor_rounding));
  shared.sync();
}

QStringList available_qt_styles() {
  return sorted_style_keys();
}

void apply_pre_app_startup(const AppStartupConfig& config) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
  if (!config.organization_name.isEmpty()) {
    QCoreApplication::setOrganizationName(config.organization_name);
  }
  if (!config.application_name.isEmpty()) {
    QCoreApplication::setApplicationName(config.application_name);
  }
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      config.hidpi.scale_factor_rounding);
}

void apply_post_app_startup(QApplication& app, const AppStartupConfig& config) {
  apply_style_if_available(app, config.preferred_style);

  if (!config.window_icon_resource.isEmpty()) {
    const QIcon icon(config.window_icon_resource);
    if (!icon.isNull()) {
      app.setWindowIcon(icon);
    }
  }
}

int small_icon_extent(const QWidget* reference) {
  return std::max(kBaseSmallIconExtent,
                  style_metric(QStyle::PM_SmallIconSize,
                               reference,
                               kBaseSmallIconExtent));
}

int toolbar_icon_extent(bool large_buttons, const QWidget* reference) {
  const int small = small_icon_extent(reference);
  if (!large_buttons) {
    return small;
  }
  return std::max(kBaseLargeToolbarIconExtent, small + 8);
}

int file_list_icon_extent(bool large_icons, const QWidget* reference) {
  const int small = small_icon_extent(reference);
  if (!large_icons) {
    return small;
  }
  return std::max(kBaseLargeListIconExtent, small * 3);
}

QSize file_list_grid_size(bool large_icons, const QWidget* reference) {
  const int icon_extent = file_list_icon_extent(large_icons, reference);
  const int font_height = effective_font_height(reference);
  if (large_icons) {
    const int width = icon_extent + std::max(64, font_height * 4);
    const int height = icon_extent + std::max(20, font_height + 8);
    return QSize(width, height);
  }
  const int width = icon_extent + std::max(56, font_height * 4);
  const int height = std::max(icon_extent + 6, font_height + 6);
  return QSize(width, height);
}

int dialog_button_min_width(const QWidget* reference) {
  const int style_margin = style_metric(QStyle::PM_ButtonMargin, reference, 6);
  const int min_from_font = effective_font_height(reference) * 6;
  return std::max({kBaseDialogButtonMinWidth, min_from_font, style_margin * 12});
}

int dialog_button_min_height(const QWidget* reference) {
  const int min_from_font = effective_font_height(reference) + 12;
  return std::max(kBaseDialogButtonMinHeight, min_from_font);
}

void apply_dialog_button_baseline(QDialogButtonBox* button_box) {
  if (button_box == nullptr) {
    return;
  }
  const int min_width = dialog_button_min_width(button_box);
  const int min_height = dialog_button_min_height(button_box);
  const QList<QAbstractButton*> buttons = button_box->buttons();
  for (QAbstractButton* button : buttons) {
    auto* push = qobject_cast<QPushButton*>(button);
    if (push == nullptr) {
      continue;
    }
    push->setMinimumSize(min_width, min_height);
  }
}

}  // namespace z7::platform::qt
