#pragma once

#include "shell_integration_menu.h"

#include <QString>

namespace z7::shell_integration::menu_internal {

QString locale_key_from_hint(QString locale_hint);

QString open_as_type_for_action(const QString& action_id);
QString crc_method_for_action(const QString& action_id);
bool action_visible_in_config(const ShellIntegrationConfig& config,
                              const QString& action_or_group);
QString menu_title_for_action(const QString& action_id,
                              const ShellIntegrationMenuPlan& plan,
                              const QString& locale_key);
void append_action_if_visible(ShellIntegrationMenuPlan* plan,
                              const ShellIntegrationConfig& config,
                              const QString& action_id,
                              const QString& locale_key);

}  // namespace z7::shell_integration::menu_internal
