// src/ui/gui/src/gui_task_runner_helpers.h
// Role: Shared helper declarations for gui_task_runner compile units.

#pragma once

#include <QString>

#include "archive_session.h"
#include "gui_task_runner.h"
#include "gui_task_spec.h"

class QWidget;

namespace z7::ui::gui {

    QString progress_header_for_spec(GuiTaskSpec const& spec, QString const& base_title);
    bool build_archive_request(GuiTaskSpec const& spec, GuiTaskRunResult* out, z7::app::ArchiveRequest* request);
    z7::app::OverwriteDecision show_overwrite_prompt_dialog(QWidget* parent, z7::app::OverwritePrompt const& prompt);
    z7::app::PasswordReply show_password_prompt_dialog(QWidget* parent, z7::app::PasswordPrompt const& prompt);
    z7::app::ChoiceReply show_choice_prompt_dialog(QWidget* parent, z7::app::ChoicePrompt const& prompt);
    z7::app::MemoryLimitReply show_memory_limit_prompt_dialog(QWidget* parent,
                                                              z7::app::MemoryLimitPrompt const& prompt);

} // namespace z7::ui::gui
