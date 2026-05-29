#include <QDir>
#include <QStringList>

#include "internal.h"
#include "portable_settings.h"

namespace z7::macos_integration::capi_internal
{
    namespace
    {

        bool has_contents_macos_tail(QString const& process_dir_path, QString* container_name_out)
        {
            QString const cleaned = QDir::cleanPath(process_dir_path);
            QStringList const parts = cleaned.split(QDir::separator(), Qt::SkipEmptyParts);
            if (parts.size() < 3)
            {
                return false;
            }
            if (parts.at(parts.size() - 1) != QStringLiteral("MacOS"))
            {
                return false;
            }
            if (parts.at(parts.size() - 2) != QStringLiteral("Contents"))
            {
                return false;
            }
            if (container_name_out != nullptr)
            {
                *container_name_out = parts.at(parts.size() - 3);
            }
            return true;
        }

    } // namespace

    QString bundled_program_path_from_process_dir(QString const& process_dir_path, QString const& program_name)
    {
        QString const cleaned_process_dir = QDir::cleanPath(process_dir_path);
        QString container_name;
        if (has_contents_macos_tail(cleaned_process_dir, &container_name))
        {
            QDir const process_dir(cleaned_process_dir);
            if (container_name.endsWith(QStringLiteral(".appex"), Qt::CaseInsensitive))
            {
                return QDir::cleanPath(
                    process_dir.absoluteFilePath(QStringLiteral("../../../../MacOS/%1").arg(program_name)));
            }
            if (container_name.endsWith(QStringLiteral(".xpc"), Qt::CaseInsensitive))
            {
                return QDir::cleanPath(
                    process_dir.absoluteFilePath(QStringLiteral("../../../../../../../MacOS/%1").arg(program_name)));
            }
            if (container_name.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive))
            {
                return QDir::cleanPath(process_dir.absoluteFilePath(program_name));
            }
        }
        return QDir(cleaned_process_dir).absoluteFilePath(program_name);
    }

    ShellIntegrationConfig runtime_config_from_snapshot(MacOSIntegrationConfigSnapshot const& snapshot)
    {
        ShellIntegrationConfig config;
        config.enabled = snapshot.enabled;
        config.visible_actions_configured = snapshot.visible_actions_configured;
        config.visible_actions = snapshot.visible_actions;
        config.cascaded_menu = snapshot.cascaded_menu;
        config.show_menu_icons = snapshot.show_menu_icons;
        config.locale_preferred = snapshot.locale_preferred;
        return config;
    }

    bool ensure_portable_settings(z7_mi_session_t* session, QString* error_message)
    {
        Q_UNUSED(session);
        QString init_error;
        if (!z7::platform::qt::initialize_portable_settings(&init_error))
        {
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("Cannot initialize portable settings: %1").arg(init_error);
            }
            return false;
        }
        return true;
    }

} // namespace z7::macos_integration::capi_internal
