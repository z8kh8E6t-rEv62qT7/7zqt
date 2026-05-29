// src/ui/filemanager/src/archive_process_runner/core_prompts.h
// Role: Default ArchiveProcessRunner interaction prompt declarations.

#pragma once

#include "archive_session.h"

class QWidget;

namespace z7::ui::filemanager {

z7::app::OverwriteDecision show_default_overwrite_prompt(
    const z7::app::OverwritePrompt& prompt);
z7::app::PasswordReply show_default_password_prompt(
    QWidget* parent,
    const z7::app::PasswordPrompt& prompt);
z7::app::ChoiceReply show_default_choice_prompt(
    const z7::app::ChoicePrompt& prompt);
z7::app::MemoryLimitReply show_default_memory_limit_prompt(
    const z7::app::MemoryLimitPrompt& prompt);

}  // namespace z7::ui::filemanager
