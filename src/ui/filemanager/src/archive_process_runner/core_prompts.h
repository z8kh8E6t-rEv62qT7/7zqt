// src/ui/filemanager/src/archive_process_runner/core_prompts.h
// Role: Default ArchiveProcessRunner interaction prompt declarations.

#pragma once

#include "archive_session.h"

class QWidget;

namespace z7::ui::filemanager {

    z7::app::OverwriteDecision show_default_overwrite_prompt(z7::app::OverwritePrompt const& prompt);
    z7::app::PasswordReply show_default_password_prompt(QWidget* parent, z7::app::PasswordPrompt const& prompt);
    z7::app::ChoiceReply show_default_choice_prompt(z7::app::ChoicePrompt const& prompt);
    z7::app::MemoryLimitReply show_default_memory_limit_prompt(z7::app::MemoryLimitPrompt const& prompt);

} // namespace z7::ui::filemanager
