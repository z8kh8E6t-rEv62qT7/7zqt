// src/ui/gui/src/gui_task_runner/shared.h
// Role: Shared private helpers for GuiTaskRunner blocking and async flows.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QMetaObject>
#include <QString>
#include <QStringList>

#include "archive_session.h"
#include "archive_types_extract.h"
#include "gui_task_runner.h"
#include "task_background_mode.h"
#include "task_progress_dialog_base.h"

namespace z7::ui::gui::gui_task_runner_shared {

struct ArchiveHashTaskOptions {
  QString archive_path;
  QString archive_type_hint;
  QStringList nested_archive_entries;
  QStringList entry_paths;
  QString hash_method;
};

struct ArchiveTestTaskOptions {
  QString archive_path;
  QString archive_type_hint;
  QStringList nested_archive_entries;
  QStringList entry_paths;
};

struct ArchiveExportTaskOptions {
  QString archive_path;
  QString archive_type_hint;
  QStringList nested_archive_entries;
  QStringList entry_paths;
  QString output_dir;
  z7::app::OverwriteMode overwrite_mode = z7::app::OverwriteMode::kAsk;
  z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths;
  bool eliminate_root_duplication = false;
  std::vector<z7::app::ExtractPathRemap> path_remaps;
  bool restore_file_security = false;
  z7::app::ExtractZoneIdMode zone_id_mode =
      z7::app::ExtractZoneIdMode::kNone;
  QString password;
};

struct SessionControlBindings {
  QMetaObject::Connection cancel_connection;
  QMetaObject::Connection pause_connection;
  QMetaObject::Connection resume_connection;
  QMetaObject::Connection remote_cancel_connection;
};

using PasswordPromptParentProvider = std::function<
    z7::ui::runtime_support::TaskProgressDialogBase*()>;

std::shared_ptr<z7::app::IArchiveDelegate> make_progress_dialog_delegate(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    GuiTaskRunResult* out,
    std::function<void(const z7::app::OperationOutcome&)> on_finished,
    PasswordPromptParentProvider password_prompt_parent_provider = {});

void prepare_progress_dialog(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    const QString& title,
    bool test_mode);

SessionControlBindings bind_session_controls(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    const z7::app::ArchiveSession& session,
    SharedTaskCancellation cancel_requested);

void release_session_controls(SessionControlBindings* bindings);

QString canceled_export_message();
bool is_export_canceled(const SharedTaskCancellation& cancel_requested);
QString localize_failure_message(QString message);
QString normalize_virtual_entry_path(QString entry);
QString infer_archive_format(const QString& archive_path,
                             const QString& type_hint);
QString outcome_summary(const z7::app::OperationOutcome& outcome);
int result_code_for_failure(const z7::app::ArchiveErrorDomain domain);
GuiTaskRunResult make_failure_result(int code,
                                     z7::app::ArchiveErrorDomain domain,
                                     const QString& message);
void apply_configured_extract_memory_limit(z7::app::TestRequest* request);
void apply_configured_extract_memory_limit(z7::app::ExtractRequest* request);
bool build_archive_export_task_options(
    const z7::ui::gui::ArchiveExportTaskSpec& spec,
    ArchiveExportTaskOptions* out,
    QString* error_message);
bool build_archive_hash_task_options(
    const z7::ui::gui::ArchiveHashTaskSpec& spec,
    ArchiveHashTaskOptions* out,
    QString* error_message);
bool build_archive_test_task_options(
    const z7::ui::gui::ArchiveTestTaskSpec& spec,
    ArchiveTestTaskOptions* out,
    QString* error_message);

}  // namespace z7::ui::gui::gui_task_runner_shared
