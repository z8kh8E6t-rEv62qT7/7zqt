// src/ui/filemanager/src/main_window/archive/archive_writeback.cpp
// Role: Archive writeback target planning and panel restore state helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <algorithm>

#include "common/archive_type_normalization.h"

namespace z7::ui::filemanager {

namespace {

QString normalized_archive_display_source(const QString& value) {
  return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

QString normalize_archive_update_format_token(const QString& value) {
  const QString lowered = value.trimmed().toLower();
  if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
    return QString();
  }
  return QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(value.toStdString()));
}

QString canonical_archive_update_format_from_suffix(const QString& value) {
  const QString lowered = value.trimmed().toLower();
  if (lowered == QStringLiteral("*") || lowered == QStringLiteral("#")) {
    return QString();
  }
  return QString::fromStdString(
      z7::common::canonical_archive_type_from_filename_suffix_copy(
          value.toStdString()));
}

QString resolve_archive_update_format(const QString& type_hint,
                                     const QString& current_display_source,
                                     const QString& source_archive) {
  QString format = normalize_archive_update_format_token(type_hint);
  if (!format.isEmpty()) {
    return format;
  }

  format = canonical_archive_update_format_from_suffix(
      QFileInfo(current_display_source).suffix());
  if (!format.isEmpty()) {
    return format;
  }

  format = canonical_archive_update_format_from_suffix(
      QFileInfo(source_archive).suffix());
  if (!format.isEmpty()) {
    return format;
  }

  return QStringLiteral("7z");
}

QStringList normalize_archive_add_input_paths(const QStringList& input_paths,
                                             const QString& blocked_archive_path) {
  QStringList out;
  QSet<QString> seen;
  const QString blocked_absolute =
      QFileInfo(blocked_archive_path).absoluteFilePath().toLower();
  for (const QString& raw_path : input_paths) {
    const QString trimmed = raw_path.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }

    const QFileInfo info(trimmed);
    if (!info.exists() || (!info.isFile() && !info.isDir())) {
      continue;
    }

    const QString absolute =
        QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
    if (absolute.isEmpty()) {
      continue;
    }

    const QString dedup_key = absolute.toLower();
    if (!blocked_absolute.isEmpty() && dedup_key == blocked_absolute) {
      continue;
    }
    if (seen.contains(dedup_key)) {
      continue;
    }
    seen.insert(dedup_key);
    out << absolute;
  }
  return out;
}

QVector<ArchiveAddInputItem> normalize_archive_add_input_items(
    const QVector<ArchiveAddInputItem>& input_items,
    const QString& blocked_archive_path) {
  QVector<ArchiveAddInputItem> out;
  out.reserve(input_items.size());

  QSet<QString> seen_archive_entries;
  const QString blocked_absolute =
      QFileInfo(blocked_archive_path).absoluteFilePath().toLower();
  for (const ArchiveAddInputItem& raw_item : input_items) {
    const QString source_path = raw_item.filesystem_path.trimmed();
    const QString archive_entry =
        z7::ui::archive_support::normalize_virtual_dir(raw_item.archive_entry);
    if (source_path.isEmpty() || archive_entry.isEmpty()) {
      continue;
    }

    const QFileInfo info(source_path);
    if (!info.exists() || (!info.isFile() && !info.isDir())) {
      continue;
    }

    const QString absolute =
        QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
    if (absolute.isEmpty()) {
      continue;
    }

    const QString source_key = absolute.toLower();
    if (!blocked_absolute.isEmpty() && source_key == blocked_absolute) {
      continue;
    }
    if (seen_archive_entries.contains(archive_entry)) {
      continue;
    }
    seen_archive_entries.insert(archive_entry);

    ArchiveAddInputItem item;
    item.filesystem_path = absolute;
    item.archive_entry = archive_entry;
    out.push_back(std::move(item));
  }
  return out;
}

}  // namespace

bool MainWindow::ArchiveWritebackPlan::is_valid() const {
  return !source_archive.trimmed().isEmpty() && !reopen_frames.isEmpty() &&
         !current_display_source().trimmed().isEmpty();
}

QString MainWindow::ArchiveWritebackPlan::root_display_source() const {
  if (reopen_frames.isEmpty()) {
    return QString();
  }
  return reopen_frames.front().virtual_display_source;
}

QString MainWindow::ArchiveWritebackPlan::root_type_hint() const {
  if (reopen_frames.isEmpty()) {
    return QString();
  }
  return reopen_frames.front().type_hint;
}

QString MainWindow::ArchiveWritebackPlan::current_display_source() const {
  if (reopen_frames.isEmpty()) {
    return QString();
  }
  return reopen_frames.back().virtual_display_source;
}

QString MainWindow::ArchiveWritebackPlan::current_virtual_dir() const {
  if (reopen_frames.isEmpty()) {
    return QString();
  }
  return reopen_frames.back().virtual_dir;
}

QString MainWindow::ArchiveWritebackPlan::current_type_hint() const {
  if (reopen_frames.isEmpty()) {
    return QString();
  }
  return reopen_frames.back().type_hint;
}

MainWindow::ArchiveWritebackPlan MainWindow::build_archive_writeback_plan_for_panel(
    int panel_index) const {
  ArchiveWritebackPlan plan;
  if (!in_archive_view_for_panel(panel_index)) {
    return plan;
  }

  const PanelController& panel = panel_controller(panel_index);
  plan.source_archive = QFileInfo(panel.archive.source_archive).absoluteFilePath();
  if (plan.source_archive.isEmpty()) {
    return plan;
  }
  plan.origin_dir = panel.archive.origin_dir;

  auto append_frame = [&plan](const QString& source_archive,
                              const QString& archive_entry_from_parent,
                              const QString& virtual_display_source,
                              const QString& virtual_dir,
                              const QString& type_hint) {
    ArchiveWritebackFrame frame;
    frame.archive_entry_from_parent =
        z7::ui::archive_support::normalize_virtual_dir(archive_entry_from_parent);
    frame.virtual_display_source =
        virtual_display_source.isEmpty() ? source_archive : virtual_display_source;
    frame.virtual_dir = virtual_dir;
    frame.type_hint = type_hint;
    plan.reopen_frames.push_back(std::move(frame));
  };

  for (const PanelController::ArchiveState::ParentContext& parent_ctx :
       panel.archive.parent_stack) {
    append_frame(plan.source_archive,
                 parent_ctx.archive_entry_from_parent,
                 parent_ctx.virtual_display_source,
                 parent_ctx.virtual_dir,
                 parent_ctx.type_hint);
  }
  append_frame(plan.source_archive,
               panel.archive.archive_entry_from_parent,
               panel.archive_display_source(),
               panel.archive.virtual_dir,
               panel.archive.type_hint);

  for (int i = 1; i < plan.reopen_frames.size(); ++i) {
    const QString nested_entry = plan.reopen_frames[i].archive_entry_from_parent;
    if (nested_entry.isEmpty()) {
      plan.reopen_frames.clear();
      plan.nested_archive_entries.clear();
      return plan;
    }
    plan.nested_archive_entries.push_back(nested_entry);
  }
  return plan;
}

bool MainWindow::can_add_external_files_to_archive_preview(int panel_index) const {
  if (!in_archive_view_for_panel(panel_index)) {
    return false;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (!panel.archive.current_token.is_valid()) {
    return false;
  }

  return build_archive_writeback_plan_for_panel(panel_index).is_valid();
}

bool MainWindow::start_add_external_files_to_archive_preview(
    int panel_index,
    const QStringList& input_paths,
    const QString& archive_destination_virtual_dir,
    const QString& caption) {
  if (!can_add_external_files_to_archive_preview(panel_index)) {
    return false;
  }

  const PanelController& panel = panel_controller(panel_index);
  const ArchiveWritebackPlan plan =
      build_archive_writeback_plan_for_panel(panel_index);
  if (!plan.is_valid()) {
    return false;
  }

  const QStringList normalized_input_paths =
      normalize_archive_add_input_paths(input_paths, plan.source_archive);
  if (normalized_input_paths.isEmpty()) {
    return false;
  }

  AddTaskOptions options;
  options.archive_path = plan.source_archive;
  options.format = resolve_archive_update_format(
      panel.archive.type_hint,
      plan.current_display_source(),
      plan.source_archive);
  options.session_token = panel.archive.current_token;
  options.input_paths = normalized_input_paths;
  options.directory =
      z7::ui::archive_support::normalize_virtual_dir(
          archive_destination_virtual_dir);

  const QString trimmed_caption = caption.trimmed().isEmpty()
                                      ? z7::ui::runtime_support::strip_mnemonic(
                                            z7::ui::runtime_support::L(7200))
                                      : caption.trimmed();
  const QString header = QStringLiteral("%1: %2")
                             .arg(trimmed_caption,
                                  QDir::toNativeSeparators(options.archive_path));
  const QString archive_path = options.archive_path;
  const QString archive_display_source = plan.current_display_source();
  const z7::app::ArchiveSessionToken session_token = options.session_token;
  return start_task_with_runner(
      header,
      trimmed_caption,
      [options](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_add_to_archive(options);
      },
      [this, archive_path, archive_display_source, session_token](
          bool ok,
          int,
          int,
          const QString&,
          const z7::app::OperationOutcome&) {
        if (!ok) {
          return;
        }
        reload_matching_archive_writeback_panels(
            archive_path,
            archive_display_source,
            session_token);
      });
}

bool MainWindow::start_add_mapped_files_to_archive_preview(
    const ArchiveWritebackPlan& plan,
    z7::app::ArchiveSessionToken session_token,
    const QVector<ArchiveAddInputItem>& input_items,
    const QString& caption,
    bool reload_on_success,
    const std::function<void(bool,
                             int,
                             int,
                             const QString&,
                             const z7::app::OperationOutcome&)>& finished_cb) {
  if (!plan.is_valid() || !session_token.is_valid()) {
    return false;
  }

  const QVector<ArchiveAddInputItem> normalized_input_items =
      normalize_archive_add_input_items(input_items, plan.source_archive);
  if (normalized_input_items.isEmpty()) {
    return false;
  }

  AddTaskOptions options;
  options.archive_path = plan.source_archive;
  options.format = resolve_archive_update_format(plan.current_type_hint(),
                                                 plan.current_display_source(),
                                                 plan.source_archive);
  options.session_token = session_token;
  options.input_items = normalized_input_items;

  const QString trimmed_caption = caption.trimmed().isEmpty()
                                      ? z7::ui::runtime_support::strip_mnemonic(
                                            z7::ui::runtime_support::L(7200))
                                      : caption.trimmed();
  const QString header = QStringLiteral("%1: %2")
                             .arg(trimmed_caption,
                                  QDir::toNativeSeparators(options.archive_path));
  const QString archive_path = options.archive_path;
  const QString archive_display_source = plan.current_display_source();
  return start_task_with_runner(
      header,
      trimmed_caption,
      [options](ArchiveProcessRunner* runner) {
        return runner != nullptr && runner->start_add_to_archive(options);
      },
      [this,
       archive_path,
       archive_display_source,
       session_token,
       reload_on_success,
       finished_cb](bool ok,
                    int code,
                    int native_code,
                    const QString& summary,
                    const z7::app::OperationOutcome& outcome) {
        if (ok && reload_on_success) {
          reload_matching_archive_writeback_panels(
              archive_path,
              archive_display_source,
              session_token);
        }
        if (finished_cb) {
          finished_cb(ok, code, native_code, summary, outcome);
        }
      });
}

bool MainWindow::PanelController::matches_archive_writeback_target(
    const QString& source_archive,
    const QString& target_display_source) const {
  return in_archive_view() &&
         QFileInfo(archive.source_archive).absoluteFilePath() ==
             QFileInfo(source_archive).absoluteFilePath() &&
         normalized_archive_display_source(archive_display_source()) ==
             normalized_archive_display_source(target_display_source);
}

void MainWindow::PanelController::apply_restored_writeback_plan(
    const ArchiveWritebackPlan& plan,
    const QVector<z7::app::ArchiveSessionToken>& opened_tokens) {
  archive.parent_stack.clear();

  const int parent_count =
      plan.reopen_frames.isEmpty() ? 0 : static_cast<int>(plan.reopen_frames.size()) - 1;
  const int opened_parent_count =
      opened_tokens.isEmpty() ? 0 : static_cast<int>(opened_tokens.size()) - 1;
  const int restored_parent_count = std::min(parent_count, opened_parent_count);
  for (int i = 0; i < restored_parent_count; ++i) {
    ArchiveState::ParentContext parent_ctx;
    parent_ctx.archive_path = plan.source_archive;
    parent_ctx.archive_entry_from_parent = plan.reopen_frames[i].archive_entry_from_parent;
    parent_ctx.virtual_display_source = plan.reopen_frames[i].virtual_display_source;
    parent_ctx.virtual_dir = plan.reopen_frames[i].virtual_dir;
    parent_ctx.origin_dir = plan.origin_dir;
    parent_ctx.type_hint = plan.reopen_frames[i].type_hint;
    parent_ctx.session_token = opened_tokens[i];
    archive.parent_stack.push_back(std::move(parent_ctx));
  }

  archive.archive_entry_from_parent =
      plan.reopen_frames.isEmpty() ? QString() : plan.reopen_frames.back().archive_entry_from_parent;
  archive.temp_session.clear();
}

void MainWindow::restore_archive_writeback_plan_for_panel(
    int panel_index,
    const ArchiveWritebackPlan& plan,
    const std::function<void(bool)>& finished_cb) {
  if (!plan.is_valid()) {
    if (finished_cb) {
      finished_cb(false);
    }
    return;
  }

  QVector<z7::app::ArchiveSessionToken> tokens_to_close;
  panel_controller(panel_index).clear_archive_view_state(
      [this, panel_index](const QSharedPointer<ArchiveTempSession>& session) {
        release_archive_temp_session_for_panel_close(panel_index, session);
      },
      [&tokens_to_close](z7::app::ArchiveSessionToken token) {
        if (token.is_valid()) {
          tokens_to_close.push_back(token);
        }
      });
  close_archive_sessions_async(
      filter_archive_session_tokens_for_panel_close(panel_index,
                                                    std::move(tokens_to_close)));

  struct ArchiveWritebackRestoreTask final
      : public std::enable_shared_from_this<ArchiveWritebackRestoreTask> {
    QPointer<MainWindow> owner;
    int panel_index = 0;
    ArchiveWritebackPlan plan;
    QVector<z7::app::ArchiveSessionToken> opened_tokens;
    std::function<void(bool)> finished;

    void start() {
      start_root_open();
    }

    void start_root_open() {
      if (owner.isNull()) {
        finish(false, false);
        return;
      }

      auto out_session_result =
          std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();
      const QString root_display_source = plan.root_display_source();
      const QString header = QStringLiteral("%1: %2")
                                 .arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), root_display_source);
      const bool started = owner->start_task_with_runner(
          header,
          z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
          [this, out_session_result](ArchiveProcessRunner* runner) {
            return runner != nullptr &&
                   runner->start_open_from_path(
                       plan.source_archive,
                       plan.root_type_hint(),
                       out_session_result);
          },
          [self = shared_from_this(), out_session_result](bool ok,
                                                          int,
                                                          int,
                                                          const QString&,
                                                          const z7::app::OperationOutcome&) {
            if (!ok || out_session_result == nullptr ||
                !out_session_result->has_value() ||
                !out_session_result->value().token.is_valid()) {
              self->finish(false, true);
              return;
            }
            self->opened_tokens.push_back(out_session_result->value().token);
            if (self->plan.nested_archive_entries.isEmpty()) {
              self->start_list_reload();
              return;
            }
            self->start_nested_open(0);
          },
          RunnerTaskUiMode::kSilent,
          [](int, const QString&) { return false; });
      if (!started) {
        finish(false, false);
      }
    }

    void start_nested_open(int index) {
      if (owner.isNull() ||
          index < 0 ||
          index >= plan.nested_archive_entries.size() ||
          opened_tokens.isEmpty()) {
        finish(false, false);
        return;
      }

      auto out_session_result =
          std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();
      const QString child_display_source =
          plan.reopen_frames[index + 1].virtual_display_source;
      const bool started = owner->start_task_with_runner(
          QStringLiteral("%1: %2")
              .arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), child_display_source),
          z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
          [this,
           index,
           out_session_result,
           child_display_source](ArchiveProcessRunner* runner) {
            return runner != nullptr &&
                   runner->start_open_nested_by_path(
                       opened_tokens.back(),
                       plan.nested_archive_entries[index],
                       plan.reopen_frames[index + 1].type_hint,
                       0,
                       child_display_source,
                       out_session_result);
          },
          [self = shared_from_this(), index, out_session_result](bool ok,
                                                                 int,
                                                                 int,
                                                                 const QString&,
                                                                 const z7::app::OperationOutcome&) {
            if (!ok || out_session_result == nullptr ||
                !out_session_result->has_value() ||
                !out_session_result->value().token.is_valid()) {
              self->finish(false, true);
              return;
            }
            self->opened_tokens.push_back(out_session_result->value().token);
            if (index + 1 >= self->plan.nested_archive_entries.size()) {
              self->start_list_reload();
              return;
            }
            self->start_nested_open(index + 1);
          },
          RunnerTaskUiMode::kSilent,
          [](int, const QString&) { return false; });
      if (!started) {
        finish(false, false);
      }
    }

    void start_list_reload() {
      if (owner.isNull() || opened_tokens.isEmpty()) {
        finish(false, false);
        return;
      }

      auto out_list_result =
          std::make_shared<std::optional<z7::app::ListResult>>();
      const bool recursive_dirs =
          !owner.isNull() &&
          owner->panel_controller(panel_index).model != nullptr &&
          owner->panel_controller(panel_index).model->flat_view();
      const bool started = owner->start_task_with_runner(
          QStringLiteral("%1: %2")
              .arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                   plan.current_display_source()),
          z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
          [this, recursive_dirs, out_list_result](ArchiveProcessRunner* runner) {
            return runner != nullptr &&
                   runner->start_list_in_session(
                       opened_tokens.back(),
                       plan.current_virtual_dir(),
                       recursive_dirs,
                       true,
                       out_list_result);
          },
          [self = shared_from_this(), out_list_result](bool ok,
                                                       int,
                                                       int,
                                                       const QString&,
                                                       const z7::app::OperationOutcome&) {
            if (!ok || out_list_result == nullptr || !out_list_result->has_value()) {
              self->finish(false, true);
              return;
            }
            self->apply_list_result(out_list_result->value());
          },
          RunnerTaskUiMode::kSilent,
          [](int, const QString&) { return false; });
      if (!started) {
        finish(false, false);
      }
    }

    void apply_list_result(const z7::app::ListResult& list_result) {
      if (owner.isNull() || opened_tokens.isEmpty()) {
        finish(false, false);
        return;
      }

      const bool loaded = owner->apply_archive_list_result_for_panel(
          panel_index,
          plan.source_archive,
          plan.current_virtual_dir(),
          plan.origin_dir,
          plan.current_type_hint(),
          list_result,
          true,
          opened_tokens.back(),
          plan.current_display_source());
      if (!loaded) {
        finish(false, true);
        return;
      }

      MainWindow::PanelController& panel = owner->panel_controller(panel_index);
      panel.apply_restored_writeback_plan(plan, opened_tokens);
      owner->apply_archive_preview_columns_visibility_for_panel(panel_index);
      finish(true, false);
    }

    void finish(bool ok, bool show_warning) {
      if (!ok) {
        if (!owner.isNull()) {
          QPointer<MainWindow> window = owner;
          const int restore_panel_index = panel_index;
          MainWindow::PanelController::ArchiveFilesystemExitTransition exit_transition =
              window->panel_controller(restore_panel_index)
                  .begin_exit_archive_view_to_filesystem(
                      plan.origin_dir,
                      [window, restore_panel_index](
                          const QSharedPointer<MainWindow::ArchiveTempSession>& session) {
                        if (!window.isNull()) {
                          window->release_archive_temp_session_for_panel_close(
                              restore_panel_index,
                              session);
                        }
                      });
          QVector<z7::app::ArchiveSessionToken> tokens_to_close = opened_tokens;
          tokens_to_close += exit_transition.tokens_to_close;
          tokens_to_close =
              owner->filter_archive_session_tokens_for_panel_close(
                  restore_panel_index,
                  std::move(tokens_to_close));
          owner->close_archive_sessions_async(
              std::move(tokens_to_close),
              [window, restore_panel_index, exit_transition](bool) {
                if (window.isNull()) {
                  return;
                }
                if (!exit_transition.target_directory.isEmpty()) {
                  window->set_current_directory_for_panel(
                      restore_panel_index,
                      exit_transition.target_directory);
                  return;
                }
                if (exit_transition.refresh_directory_after_exit) {
                  window->refresh_directory_for_panel(restore_panel_index);
                }
              });
          owner->apply_archive_preview_columns_visibility_for_panel(
              restore_panel_index);
          if (show_warning) {
            QMessageBox::warning(
                owner,
                z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                QStringLiteral("Archive was updated, but the archive view could not be reloaded."));
          }
        }
      }
      if (finished) {
        finished(ok);
      }
    }
  };

  auto task = std::make_shared<ArchiveWritebackRestoreTask>();
  task->owner = this;
  task->panel_index = panel_index;
  task->plan = plan;
  task->finished = finished_cb;
  task->start();
}

}  // namespace z7::ui::filemanager
