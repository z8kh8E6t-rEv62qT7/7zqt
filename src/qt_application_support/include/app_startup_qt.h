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
  Qt::HighDpiScaleFactorRoundingPolicy scale_factor_rounding =
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough;
};

struct AppStartupConfig final {
  QString organization_name = QStringLiteral("7z2600");
  QString application_name;
  QString window_icon_resource = QStringLiteral(":/z7/fm-icons/FM.ico");
  QString preferred_style = QStringLiteral("Fusion");
  HiDpiPolicy hidpi;
};

AppStartupConfig default_startup_config(StartupAppKind app_kind);
AppStartupConfig startup_config_with_persisted_overrides(
    StartupAppKind app_kind,
    const QString& argv0_hint = QString());
void persist_startup_overrides(const AppStartupConfig& config);
QStringList available_qt_styles();

void apply_pre_app_startup(const AppStartupConfig& config);
void apply_post_app_startup(QApplication& app, const AppStartupConfig& config);

int small_icon_extent(const QWidget* reference = nullptr);
int toolbar_icon_extent(bool large_buttons, const QWidget* reference = nullptr);
int file_list_icon_extent(bool large_icons, const QWidget* reference = nullptr);
QSize file_list_grid_size(bool large_icons, const QWidget* reference = nullptr);

int dialog_button_min_width(const QWidget* reference = nullptr);
int dialog_button_min_height(const QWidget* reference = nullptr);
void apply_dialog_button_baseline(QDialogButtonBox* button_box);

}  // namespace z7::platform::qt
