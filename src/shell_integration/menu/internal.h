#pragma once

#include <QString>

#include "shell_integration_menu.h"

namespace z7::shell_integration::menu_internal {

    QString locale_key_from_hint(QString language_hint);

    QString open_as_type_for_action(QString const& action_id);
    QString crc_method_for_action(QString const& action_id);
    bool action_visible_in_config(ShellIntegrationConfig const& config, QString const& action_or_group);
    QString
    menu_title_for_action(QString const& action_id, ShellIntegrationMenuPlan const& plan, QString const& locale_key);
    void append_action_if_visible(ShellIntegrationMenuPlan* plan,
                                  ShellIntegrationConfig const& config,
                                  QString const& action_id,
                                  QString const& locale_key);

} // namespace z7::shell_integration::menu_internal
