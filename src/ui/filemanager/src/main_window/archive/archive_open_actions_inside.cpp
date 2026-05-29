// src/ui/filemanager/src/main_window/archive/archive_open_actions_inside.cpp
// Role: Open-inside archive navigation actions.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

void MainWindow::open_archive_file_inside_for_panel(int panel_index,
                                                    const QString& entry_path,
                                                    const QString& archive_type_hint) {
  PanelController& panel = panel_controller(panel_index);
  if (!in_archive_view_for_panel(panel_index) ||
      panel.archive.source_archive.isEmpty() ||
      !panel.archive.current_token.is_valid()) {
    return;
  }

  const QString normalized_entry =
      z7::ui::archive_support::normalize_virtual_dir(entry_path);
  if (normalized_entry.isEmpty()) {
    return;
  }

  const QString override_hint = archive_type_hint.trimmed();
  const QString effective_hint =
      override_hint.isEmpty() ? panel.archive.type_hint : override_hint;
  const QString archive_path = panel.archive.source_archive;
  const QString origin_dir = panel.archive.origin_dir;
  const QString display_source = panel.archive_display_source();
  QString nested_display = display_source;
  if (nested_display.isEmpty()) {
    nested_display = archive_path;
  }
  nested_display += QLatin1Char('/');
  nested_display += QDir::toNativeSeparators(normalized_entry);
  auto out_session_result =
      std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();

  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), nested_display),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
      [parent_token = panel.archive.current_token,
       normalized_entry,
       effective_hint,
       nested_display,
       out_session_result](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_open_nested_by_path(
                   parent_token,
                   normalized_entry,
                   effective_hint,
                   0,
                   nested_display,
                   out_session_result);
      },
      [this,
       panel_index,
       normalized_entry,
       effective_hint,
       nested_display,
       origin_dir,
       archive_path,
       out_session_result](bool ok,
                           int,
                           int,
                           const QString&,
                           const z7::app::OperationOutcome&) {
        if (!ok) {
          return;
        }
        if (out_session_result == nullptr || !out_session_result->has_value() ||
            !out_session_result->value().token.is_valid()) {
          return;
        }

        PanelController& current_panel = panel_controller(panel_index);
        current_panel.push_current_archive_to_parent_stack();

        const z7::app::ArchiveSessionToken child_token =
            out_session_result->value().token;
        auto nested_open_finished = std::make_shared<bool>(false);
        const auto rollback_nested_open =
            [this, panel_index, child_token, nested_open_finished]() {
              if (*nested_open_finished) {
                return;
              }
              *nested_open_finished = true;
              PanelController& panel = panel_controller(panel_index);
              panel.discard_last_parent_archive_frame();
              close_archive_sessions_async(
                  QVector<z7::app::ArchiveSessionToken>{child_token});
            };
        const auto commit_nested_open =
            [this, panel_index, normalized_entry, nested_open_finished]() {
              if (*nested_open_finished) {
                return;
              }
              *nested_open_finished = true;
              PanelController& panel = panel_controller(panel_index);
              panel.archive.archive_entry_from_parent = normalized_entry;
              panel.archive.temp_session.clear();
              set_active_panel(panel_index);
            };

        const bool started = load_archive_virtual_directory_for_panel(
            panel_index,
            archive_path,
            QString(),
            origin_dir,
            effective_hint,
            true,
            [rollback_nested_open, commit_nested_open](bool loaded) {
              if (!loaded) {
                rollback_nested_open();
                return;
              }
              commit_nested_open();
            },
            false,
            {},
            child_token,
            nested_display);
        if (!started) {
          // load_archive_virtual_directory_for_panel() delivers finished(false)
          // when it rejects startup, so rollback is centralized in that callback.
          return;
        }
      });
}

void MainWindow::open_archive_inside(const QString& archive_path,
                                     const QString& archive_type_hint) {
  open_archive_inside_for_panel(active_panel_index_,
                                archive_path,
                                archive_type_hint);
}

void MainWindow::open_archive_inside_for_panel(int panel_index,
                                               const QString& archive_path,
                                               const QString& archive_type_hint,
                                               std::function<void()> open_failure_fallback) {
  const QFileInfo archive_info(archive_path);
  if (!archive_info.exists() || !archive_info.isFile()) {
    QMessageBox::warning(this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), archive_path);
    return;
  }

  const QString source_archive = archive_info.absoluteFilePath();
  const QString origin_dir = archive_info.absolutePath();
  const QString type_hint = archive_type_hint.trimmed();
  if (in_archive_view_for_panel(panel_index)) {
    close_archive_view_for_panel(
        panel_index,
        [this, panel_index, source_archive, type_hint, open_failure_fallback](bool ok) mutable {
          if (!ok) {
            if (open_failure_fallback) {
              open_failure_fallback();
            }
            return;
          }
          open_archive_inside_for_panel(
              panel_index,
              source_archive,
              type_hint,
              std::move(open_failure_fallback));
        });
    return;
  }
  auto out_session_result =
      std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();

  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), source_archive),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
      [source_archive, type_hint, out_session_result](
          ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_open_from_path(
                   source_archive,
                   type_hint,
                   out_session_result);
      },
      [this,
       panel_index,
       source_archive,
       origin_dir,
       type_hint,
       out_session_result,
       open_failure_fallback](bool ok,
                              int,
                              int,
                              const QString&,
                              const z7::app::OperationOutcome&) {
        if (!ok) {
          if (open_failure_fallback) {
            open_failure_fallback();
          }
          return;
        }
        if (out_session_result == nullptr || !out_session_result->has_value() ||
            !out_session_result->value().token.is_valid()) {
          return;
        }

        const z7::app::ArchiveSessionToken session_token =
            out_session_result->value().token;
        const bool started = load_archive_virtual_directory_for_panel(
            panel_index,
            source_archive,
            QString(),
            origin_dir,
            type_hint,
            true,
            [this, panel_index, session_token](bool loaded) {
              if (!loaded) {
                close_archive_sessions_async(
                    QVector<z7::app::ArchiveSessionToken>{session_token});
                return;
              }
              panel_controller(panel_index).archive.archive_entry_from_parent.clear();
              set_active_panel(panel_index);
            },
            false,
            {},
            session_token,
            source_archive);
        if (!started) {
          close_archive_sessions_async(
              QVector<z7::app::ArchiveSessionToken>{session_token});
        }
      },
      RunnerTaskUiMode::kSilent,
      open_failure_fallback
          ? std::function<bool(int, const QString&)>(
                [](int, const QString&) { return false; })
          : std::function<bool(int, const QString&)>());
}

}  // namespace z7::ui::filemanager
