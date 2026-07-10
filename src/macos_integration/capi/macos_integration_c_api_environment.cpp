#include <QDir>
#include <QStringList>

#include "internal.h"

namespace z7::macos_integration::capi_internal {
    namespace {

        bool has_contents_macos_tail(QString const& process_dir_path, QString* container_name_out) {
            QString const cleaned = QDir::cleanPath(process_dir_path);
            QStringList const parts = cleaned.split(QDir::separator(), Qt::SkipEmptyParts);
            if (parts.size() < 3) {
                return false;
            }
            if (parts.at(parts.size() - 1) != QStringLiteral("MacOS")) {
                return false;
            }
            if (parts.at(parts.size() - 2) != QStringLiteral("Contents")) {
                return false;
            }
            if (container_name_out != nullptr) {
                *container_name_out = parts.at(parts.size() - 3);
            }
            return true;
        }

    } // namespace

    QString bundled_program_path_from_process_dir(QString const& process_dir_path, QString const& program_name) {
        QString const cleaned_process_dir = QDir::cleanPath(process_dir_path);
        QString container_name;
        if (has_contents_macos_tail(cleaned_process_dir, &container_name)) {
            QDir const process_dir(cleaned_process_dir);
            if (container_name.endsWith(QStringLiteral(".appex"), Qt::CaseInsensitive)) {
                return QDir::cleanPath(
                    process_dir.absoluteFilePath(QStringLiteral("../../../../MacOS/%1").arg(program_name)));
            }
            if (container_name.endsWith(QStringLiteral(".xpc"), Qt::CaseInsensitive)) {
                return QDir::cleanPath(
                    process_dir.absoluteFilePath(QStringLiteral("../../../../../../../MacOS/%1").arg(program_name)));
            }
            if (container_name.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
                return QDir::cleanPath(process_dir.absoluteFilePath(program_name));
            }
        }
        return QDir(cleaned_process_dir).absoluteFilePath(program_name);
    }

    ShellIntegrationConfig runtime_config_from_settings(MacOSIntegrationConfig const& settings) {
        ShellIntegrationConfig config;
        config.enabled = settings.enabled;
        config.visible_actions_configured = settings.visible_actions_configured;
        config.visible_actions = settings.visible_actions;
        config.cascaded_menu = settings.cascaded_menu;
        config.show_menu_icons = settings.show_menu_icons;
        config.locale_preferred = settings.locale_preferred;
        return config;
    }

} // namespace z7::macos_integration::capi_internal
