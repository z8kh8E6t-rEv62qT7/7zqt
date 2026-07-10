#include "app_startup_qt.h"

#include <QAbstractButton>
#include <QApplication>
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
        constexpr char const* kSharedSettingsAppName = "7z-shared";
        constexpr char const* kStartupPreferredStyleKey = "Qt/Startup/PreferredStyle";
        constexpr char const* kStartupHiDpiRoundingKey = "Qt/Startup/HiDpiRoundingPolicy";

        QStyle* effective_style(QWidget const* reference) {
            if (reference != nullptr && reference->style() != nullptr) {
                return reference->style();
            }
            if (QApplication::instance() != nullptr) {
                return QApplication::style();
            }
            return nullptr;
        }

        int effective_font_height(QWidget const* reference) {
            if (reference != nullptr) {
                return reference->fontMetrics().height();
            }
            if (QApplication::instance() != nullptr) {
                return static_cast<int>(std::ceil(QFontMetricsF(QApplication::font()).height()));
            }
            return 14;
        }

        int style_metric(QStyle::PixelMetric metric, QWidget const* reference, int default_value) {
            QStyle* style = effective_style(reference);
            if (style == nullptr) {
                return default_value;
            }
            int const value = style->pixelMetric(metric, nullptr, reference);
            if (value <= 0) {
                return default_value;
            }
            return value;
        }

        QString resolve_style_name(QString const& requested) {
            if (requested.trimmed().isEmpty()) {
                return QString();
            }
            QStringList const keys = QStyleFactory::keys();
            for (QString const& key : keys) {
                if (QString::compare(key, requested, Qt::CaseInsensitive) == 0) {
                    return key;
                }
            }
            return QString();
        }

        bool apply_style_if_available(QApplication& app, QString const& style_name) {
            QString const resolved = resolve_style_name(style_name);
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
            std::sort(keys.begin(), keys.end(), [](QString const& lhs, QString const& rhs) {
                return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
            });
            return keys;
        }

        QString startup_settings_path_from_argv0_hint(QString const& argv0_hint) {
            QString const trimmed_hint = argv0_hint.trimmed();
            if (trimmed_hint.isEmpty()) {
                return QString();
            }

            QFileInfo const exe_info(trimmed_hint);
            QString const absolute_exe_path = exe_info.isAbsolute()
                                                ? exe_info.absoluteFilePath()
                                                : QFileInfo(QDir::current(), trimmed_hint).absoluteFilePath();
            if (absolute_exe_path.isEmpty()) {
                return QString();
            }

            QFileInfo const absolute_info(absolute_exe_path);
            if (!absolute_info.exists() || !absolute_info.isFile()) {
                return QString();
            }

            QString const root_dir =
                portable_settings_internal::default_portable_settings_root_for_executable_hint(absolute_exe_path);
            if (root_dir.trimmed().isEmpty()) {
                return QString();
            }
            return QDir(root_dir).filePath(QStringLiteral("settings.json"));
        }

        QJsonObject read_shared_settings_json(QString const& settings_file_path) {
            if (settings_file_path.trimmed().isEmpty()) {
                return QJsonObject{};
            }

            QFile file(settings_file_path);
            if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
                return QJsonObject{};
            }

            QJsonParseError parse_error;
            QJsonDocument const document = QJsonDocument::fromJson(file.readAll(), &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
                return QJsonObject{};
            }

            return document.object().value(QStringLiteral("shared")).toObject();
        }

        Qt::HighDpiScaleFactorRoundingPolicy parse_rounding_policy(QString const& raw_value, bool* ok = nullptr) {
            QString const value = raw_value.trimmed().toLower();
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
            if (value == QStringLiteral("round_prefer_floor") || value == QStringLiteral("roundpreferfloor")) {
                if (ok != nullptr) {
                    *ok = true;
                }
                return Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor;
            }
            if (value == QStringLiteral("pass_through") || value == QStringLiteral("passthrough")) {
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

        void apply_startup_overrides_from_shared_json(QJsonObject const& shared, AppStartupConfig* config) {
            if (config == nullptr) {
                return;
            }

            if (shared.contains(QString::fromLatin1(kStartupPreferredStyleKey))) {
                config->preferred_style =
                    shared.value(QString::fromLatin1(kStartupPreferredStyleKey)).toString().trimmed();
            }
            if (shared.contains(QString::fromLatin1(kStartupHiDpiRoundingKey))) {
                bool ok = false;
                QString const policy_value = shared.value(QString::fromLatin1(kStartupHiDpiRoundingKey)).toString();
                Qt::HighDpiScaleFactorRoundingPolicy const policy = parse_rounding_policy(policy_value, &ok);
                if (ok) {
                    config->hidpi.scale_factor_rounding = policy;
                }
            }
        }

        QString startup_organization_name_or_default(QString const& configured) {
            QString const trimmed = configured.trimmed();
            return trimmed.isEmpty() ? QStringLiteral("7z2600") : trimmed;
        }

    } // namespace

    AppStartupConfig default_startup_config(StartupAppKind app_kind) {
        AppStartupConfig config;
        config.application_name =
            (app_kind == StartupAppKind::kFileManager) ? QStringLiteral("7zFM") : QStringLiteral("7zFM");
        return config;
    }

    AppStartupConfig startup_config_with_persisted_overrides(StartupAppKind app_kind, QString const& argv0_hint) {
        AppStartupConfig config = default_startup_config(app_kind);

        if (QCoreApplication::instance() != nullptr) {
            PortableSettings const shared(startup_organization_name_or_default(config.organization_name),
                                          QString::fromLatin1(kSharedSettingsAppName));
            if (shared.contains(QString::fromLatin1(kStartupPreferredStyleKey))) {
                config.preferred_style =
                    shared.value(QString::fromLatin1(kStartupPreferredStyleKey)).toString().trimmed();
            }
            if (shared.contains(QString::fromLatin1(kStartupHiDpiRoundingKey))) {
                bool ok = false;
                Qt::HighDpiScaleFactorRoundingPolicy const policy =
                    parse_rounding_policy(shared.value(QString::fromLatin1(kStartupHiDpiRoundingKey)).toString(), &ok);
                if (ok) {
                    config.hidpi.scale_factor_rounding = policy;
                }
            }
            return config;
        }

        QString const settings_path = startup_settings_path_from_argv0_hint(argv0_hint);
        apply_startup_overrides_from_shared_json(read_shared_settings_json(settings_path), &config);
        return config;
    }

    void persist_startup_overrides(AppStartupConfig const& config) {
        PortableSettings shared(startup_organization_name_or_default(config.organization_name),
                                QString::fromLatin1(kSharedSettingsAppName));
        shared.setValue(QString::fromLatin1(kStartupPreferredStyleKey), config.preferred_style.trimmed());
        shared.setValue(QString::fromLatin1(kStartupHiDpiRoundingKey),
                        rounding_policy_to_string(config.hidpi.scale_factor_rounding));
        shared.sync();
    }

    QStringList available_qt_styles() {
        return sorted_style_keys();
    }

    void apply_pre_app_startup(AppStartupConfig const& config) {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        if (!config.organization_name.isEmpty()) {
            QCoreApplication::setOrganizationName(config.organization_name);
        }
        if (!config.application_name.isEmpty()) {
            QCoreApplication::setApplicationName(config.application_name);
        }
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(config.hidpi.scale_factor_rounding);
    }

    void apply_post_app_startup(QApplication& app, AppStartupConfig const& config) {
        apply_style_if_available(app, config.preferred_style);

        if (!config.window_icon_resource.isEmpty()) {
            QIcon const icon(config.window_icon_resource);
            if (!icon.isNull()) {
                app.setWindowIcon(icon);
            }
        }
    }

    int small_icon_extent(QWidget const* reference) {
        return std::max(kBaseSmallIconExtent, style_metric(QStyle::PM_SmallIconSize, reference, kBaseSmallIconExtent));
    }

    int toolbar_icon_extent(bool large_buttons, QWidget const* reference) {
        int const small = small_icon_extent(reference);
        if (!large_buttons) {
            return small;
        }
        return std::max(kBaseLargeToolbarIconExtent, small + 8);
    }

    int file_list_icon_extent(bool large_icons, QWidget const* reference) {
        int const small = small_icon_extent(reference);
        if (!large_icons) {
            return small;
        }
        return std::max(kBaseLargeListIconExtent, small * 3);
    }

    QSize file_list_grid_size(bool large_icons, QWidget const* reference) {
        int const icon_extent = file_list_icon_extent(large_icons, reference);
        int const font_height = effective_font_height(reference);
        if (large_icons) {
            int const width = icon_extent + std::max(64, font_height * 4);
            int const height = icon_extent + std::max(20, font_height + 8);
            return QSize(width, height);
        }
        int const width = icon_extent + std::max(56, font_height * 4);
        int const height = std::max(icon_extent + 6, font_height + 6);
        return QSize(width, height);
    }

    int dialog_button_min_width(QWidget const* reference) {
        int const style_margin = style_metric(QStyle::PM_ButtonMargin, reference, 6);
        int const min_from_font = effective_font_height(reference) * 6;
        return std::max({kBaseDialogButtonMinWidth, min_from_font, style_margin * 12});
    }

    int dialog_button_min_height(QWidget const* reference) {
        int const min_from_font = effective_font_height(reference) + 12;
        return std::max(kBaseDialogButtonMinHeight, min_from_font);
    }

    void apply_dialog_button_baseline(QDialogButtonBox* button_box) {
        if (button_box == nullptr) {
            return;
        }
        int const min_width = dialog_button_min_width(button_box);
        int const min_height = dialog_button_min_height(button_box);
        QList<QAbstractButton*> const buttons = button_box->buttons();
        for (QAbstractButton* button : buttons) {
            auto* push = qobject_cast<QPushButton*>(button);
            if (push == nullptr) {
                continue;
            }
            push->setMinimumSize(min_width, min_height);
        }
    }

} // namespace z7::platform::qt
