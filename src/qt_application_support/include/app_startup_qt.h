#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <Qt>

class QApplication;
class QDialogButtonBox;
class QWidget;

namespace z7::platform::qt {

    enum class StartupAppKind {
        kFileManager,
        kGui
    };

    struct HiDpiPolicy final {
        Qt::HighDpiScaleFactorRoundingPolicy scale_factor_rounding = Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
    };

    struct AppStartupConfig final {
        QString organization_name = QStringLiteral("7z2600");
        QString application_name;
        QString window_icon_resource = QStringLiteral(":/z7/fm-icons/FM.ico");
        QString preferred_style = QStringLiteral("Fusion");
        HiDpiPolicy hidpi;
    };

    AppStartupConfig default_startup_config(StartupAppKind app_kind);
    AppStartupConfig startup_config_with_persisted_overrides(StartupAppKind app_kind,
                                                             QString const& argv0_hint = QString());
    void persist_startup_overrides(AppStartupConfig const& config);
    QStringList available_qt_styles();

    void apply_pre_app_startup(AppStartupConfig const& config);
    void apply_post_app_startup(QApplication& app, AppStartupConfig const& config);

    int small_icon_extent(QWidget const* reference = nullptr);
    int toolbar_icon_extent(bool large_buttons, QWidget const* reference = nullptr);
    int file_list_icon_extent(bool large_icons, QWidget const* reference = nullptr);
    QSize file_list_grid_size(bool large_icons, QWidget const* reference = nullptr);

    int dialog_button_min_width(QWidget const* reference = nullptr);
    int dialog_button_min_height(QWidget const* reference = nullptr);
    void apply_dialog_button_baseline(QDialogButtonBox* button_box);

} // namespace z7::platform::qt
