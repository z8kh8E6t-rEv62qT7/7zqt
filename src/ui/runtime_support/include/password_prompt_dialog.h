// src/ui/runtime_support/include/password_prompt_dialog.h
// Role: Original-style password prompt dialog shared by GUI task flows.

#pragma once

#include <QString>
#include <optional>

#include "archive_session.h"

class QWidget;

namespace z7::ui::runtime_support {

    std::optional<QString> show_password_prompt_dialog(QWidget* parent, z7::app::PasswordPrompt const& prompt);

} // namespace z7::ui::runtime_support
