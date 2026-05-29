// src/ui/filemanager/src/main_window/archive/archive_virtual_view.cpp
// Role: Archive virtual path normalization, display path, and list loading.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <algorithm>

namespace z7::ui::filemanager {

QString MainWindow::archive_virtual_display_path_for_panel(int panel_index) const {
  return panel_controller(panel_index).archive_virtual_display_path();
}

bool MainWindow::load_archive_virtual_directory_for_panel(
    int panel_index,
    const QString& archive_path,
    const QString& virtual_dir,
    const QString& origin_dir,
    const QString& archive_type_hint,
    bool update_origin_dir,
    const std::function<void(bool)>& finished_cb,
    bool suppress_unsupported_warning,
    const std::function<void(int, const QString&)>& failed_cb,
    z7::app::ArchiveSessionToken session_token,
    const QString& virtual_display_source) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    if (finished_cb) {
      finished_cb(false);
    }
    return false;
  }

  const QString source_archive = QFileInfo(archive_path).absoluteFilePath();
  if (source_archive.isEmpty()) {
    if (finished_cb) {
      finished_cb(false);
    }
    return false;
  }

  const QString normalized_virtual_dir =
      z7::ui::archive_support::normalize_virtual_dir(virtual_dir);
  const QString type_hint = archive_type_hint.trimmed();
  const QString effective_display_source =
      virtual_display_source.isEmpty() ? source_archive : virtual_display_source;
  const bool recursive_dirs = panel.model->flat_view();
  auto out_list_result =
      std::make_shared<std::optional<z7::app::ListResult>>();

  const bool started = start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), effective_display_source),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
      [source_archive,
       normalized_virtual_dir,
       type_hint,
       recursive_dirs,
       out_list_result,
       session_token](ArchiveProcessRunner* runner) {
        if (runner == nullptr) {
          return false;
        }
        if (session_token.is_valid()) {
          return runner->start_list_in_session(
              session_token,
              normalized_virtual_dir,
              recursive_dirs,
              true,
              out_list_result);
        }
        return runner->start_open_archive(
            source_archive,
            normalized_virtual_dir,
            type_hint,
            recursive_dirs,
            true,
            out_list_result);
      },
      [this,
       panel_index,
       source_archive,
       normalized_virtual_dir,
       origin_dir,
       type_hint,
       update_origin_dir,
       effective_display_source,
       out_list_result,
       finished_cb,
       failed_cb,
       session_token](bool ok,
                      int,
                      int error_domain,
                      const QString& summary,
                      const z7::app::OperationOutcome&) {
        if (!ok && failed_cb) {
          failed_cb(error_domain, summary);
        }
        bool loaded = false;
        if (ok && out_list_result != nullptr && out_list_result->has_value()) {
          loaded = apply_archive_list_result_for_panel(panel_index,
                                                       source_archive,
                                                       normalized_virtual_dir,
                                                       origin_dir,
                                                       type_hint,
                                                       out_list_result->value(),
                                                       update_origin_dir,
                                                       session_token,
                                                       effective_display_source);
        }
        if (finished_cb) {
          finished_cb(loaded);
        }
      },
      RunnerTaskUiMode::kSilent,
      [suppress_unsupported_warning](int error_domain, const QString&) {
        if (!suppress_unsupported_warning) {
          return true;
        }
        return static_cast<z7::app::ArchiveErrorDomain>(error_domain) !=
               z7::app::ArchiveErrorDomain::kUnsupportedFormat;
      });

  if (!started) {
    if (failed_cb) {
      failed_cb(static_cast<int>(z7::app::ArchiveErrorDomain::kUnknown),
                QStringLiteral("Failed to start archive open task."));
    }
    if (finished_cb) {
      finished_cb(false);
    }
    return false;
  }
  return true;
}

bool MainWindow::apply_archive_list_result_for_panel(
    int panel_index,
    const QString& archive_path,
    const QString& virtual_dir,
    const QString& origin_dir,
    const QString& archive_type_hint,
    const z7::app::ListResult& list_result,
    bool update_origin_dir,
    z7::app::ArchiveSessionToken session_token,
    const QString& virtual_display_source) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr) {
    return false;
  }

  const QString source_archive = QFileInfo(archive_path).absoluteFilePath();
  const QString normalized_virtual_dir =
      z7::ui::archive_support::normalize_virtual_dir(virtual_dir);
  if (!list_result.ok) {
    QString summary = z7::ui::archive_support::from_utf8_string(list_result.summary);
    if (summary.isEmpty()) {
      summary = z7::ui::archive_support::from_utf8_string(z7::app::describe_archive_error(list_result.error));
    }
    if (summary.isEmpty()) {
      summary = QStringLiteral("Archive list failed.");
    }
    if (list_result.error.domain != z7::app::ArchiveErrorDomain::kCanceled) {
      QMessageBox::warning(this,
                           z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                           stable_error_message(
                               static_cast<int>(list_result.error.domain)));
    }
    return false;
  }

  QVector<DirectoryListModel::VirtualEntry> virtual_entries;
  virtual_entries.reserve(static_cast<int>(list_result.entries.size()) + 1);

  if (display_settings_.show_dots) {
    DirectoryListModel::VirtualEntry parent_entry;
    parent_entry.display_name = QStringLiteral("..");
    parent_entry.is_dir = true;
    parent_entry.is_parent_link = true;
    virtual_entries.push_back(std::move(parent_entry));
  }

  for (const z7::app::ArchiveListEntry& item : list_result.entries) {
    const QString name = z7::ui::archive_support::normalize_virtual_dir(
        z7::ui::archive_support::from_utf8_string(item.path));
    if (name.isEmpty()) {
      continue;
    }

    DirectoryListModel::VirtualEntry entry;
    entry.path =
        z7::ui::archive_support::join_virtual_path(normalized_virtual_dir, name);
    entry.display_name = name;
    entry.is_dir = item.is_dir;
    entry.size = item.size;
    entry.packed_size = item.packed_size;
    entry.mtime_msecs_utc = item.mtime_msecs_utc;
    entry.ctime_msecs_utc = item.ctime_msecs_utc;
    entry.atime_msecs_utc = item.atime_msecs_utc;
    entry.attributes = z7::ui::archive_support::from_utf8_string(item.attributes);
    entry.encrypted = item.encrypted;
    entry.comment = z7::ui::archive_support::from_utf8_string(item.comment);
    entry.crc = item.crc;
    entry.method = z7::ui::archive_support::from_utf8_string(item.method);
    entry.characts = z7::ui::archive_support::from_utf8_string(item.characts);
    entry.host_os = z7::ui::archive_support::from_utf8_string(item.host_os);
    entry.version = z7::ui::archive_support::from_utf8_string(item.version);
    entry.volume_index = item.volume_index;
    entry.offset = item.offset;
    entry.num_sub_dirs = item.num_sub_dirs;
    entry.num_sub_files = item.num_sub_files;
    virtual_entries.push_back(std::move(entry));
  }

  panel.archive.current_token = session_token;
  panel.archive.virtual_display_source =
      virtual_display_source.isEmpty() ? source_archive : virtual_display_source;
  panel.enter_archive_view(source_archive,
                           normalized_virtual_dir,
                           origin_dir,
                           archive_type_hint.trimmed(),
                           update_origin_dir);

  const QString model_dir = panel.current_directory();
  panel.model->set_virtual_entries(model_dir, virtual_entries);
  remember_folder_history(panel.archive_virtual_display_path());
  rebind_auto_refresh_watcher_for_panel(panel_index);
  apply_archive_preview_columns_visibility_for_panel(panel_index);

  if (panel.ui.details_view != nullptr && panel.ui.details_view->selectionModel() != nullptr) {
    panel.ui.details_view->selectionModel()->clearSelection();
  }

  if (panel_index == active_panel_index_) {
    refresh_active_panel_chrome();
  } else {
    update_status_for_panel(panel_index);
  }
  return true;
}

bool MainWindow::reload_archive_virtual_directory_for_panel(
    int panel_index,
    const std::function<void(bool)>& finished_cb) {
  const PanelController& panel = panel_controller(panel_index);
  if (!in_archive_view_for_panel(panel_index)) {
    if (finished_cb) {
      finished_cb(false);
    }
    return false;
  }

  const QStringList selected_paths_snapshot = selected_filesystem_paths_including_parent_link_for_panel(panel_index);
  QString current_path_snapshot;
  int current_column_snapshot = 0;
  if (panel.model != nullptr) {
    QAbstractItemView* view = panel.current_item_view();
    if (view != nullptr) {
      const QModelIndex current_index = view->currentIndex();
      if (current_index.isValid()) {
        current_path_snapshot = panel.model->path_for_row(current_index.row());
        current_column_snapshot = current_index.column();
      }
    }
  }
  if (current_path_snapshot.isEmpty() && !selected_paths_snapshot.isEmpty()) {
    current_path_snapshot = selected_paths_snapshot.front();
  }
  const PanelController::ScrollPositionSnapshot scroll_snapshot =
      panel.capture_scroll_position();

  return load_archive_virtual_directory_for_panel(
      panel_index,
      panel.archive.source_archive,
      panel.archive.virtual_dir,
      panel.archive.origin_dir,
      panel.archive.type_hint,
      false,
      [this, panel_index, selected_paths_snapshot, current_path_snapshot,
       current_column_snapshot, scroll_snapshot, finished_cb](bool loaded) {
        if (!loaded) {
          if (finished_cb) {
            finished_cb(false);
          }
          return;
        }

        PanelController& current_panel = panel_controller(panel_index);
        if (current_panel.model == nullptr) {
          current_panel.restore_scroll_position(scroll_snapshot);
          if (current_panel.ui.details_view != nullptr) {
            current_panel.ui.details_view->refresh_hover_from_cursor();
          }
          if (finished_cb) {
            finished_cb(true);
          }
          return;
        }
        if (current_panel.ui.details_view == nullptr ||
            current_panel.ui.details_view->selectionModel() == nullptr) {
          current_panel.restore_scroll_position(scroll_snapshot);
          if (current_panel.ui.details_view != nullptr) {
            current_panel.ui.details_view->refresh_hover_from_cursor();
          }
          if (finished_cb) {
            finished_cb(true);
          }
          return;
        }

        auto find_row_by_path = [&current_panel](const QString& path) -> int {
          if (path.isEmpty() || current_panel.model == nullptr) {
            return -1;
          }
          const int row_count = current_panel.model->rowCount();
          for (int row = 0; row < row_count; ++row) {
            if (current_panel.model->path_for_row(row) == path) {
              return row;
            }
          }
          return -1;
        };

        QItemSelectionModel* selection_model = current_panel.selection_model();
        // Selection lives on the primary cell only; the Rows flag would expand
        // to every column and show up as a row-wide highlight because the
        // StructuredListView renders per-cell selection state.
        const QItemSelectionModel::SelectionFlags restore_flags =
            QItemSelectionModel::Select;
        QSet<int> restored_rows;
        for (const QString& selected_path : selected_paths_snapshot) {
          const int row = find_row_by_path(selected_path);
          if (row < 0 || restored_rows.contains(row)) {
            continue;
          }
          restored_rows.insert(row);
          const QModelIndex proxy_index = current_panel.map_source_to_proxy(
              current_panel.model->index(row, DirectoryListModel::kNameColumn));
          if (proxy_index.isValid()) {
            selection_model->select(proxy_index, restore_flags);
          }
        }

        int current_row = find_row_by_path(current_path_snapshot);
        if (current_row < 0 && !restored_rows.isEmpty()) {
          current_row = *restored_rows.begin();
        }
        if (current_row >= 0) {
          (void)current_column_snapshot;
          const QModelIndex proxy_index = current_panel.map_source_to_proxy(
              current_panel.model->index(current_row,
                                         DirectoryListModel::kNameColumn));
          if (proxy_index.isValid()) {
            selection_model->setCurrentIndex(proxy_index,
                                             QItemSelectionModel::NoUpdate);
          }
        }
        current_panel.restore_scroll_position(scroll_snapshot);
        current_panel.ui.details_view->refresh_hover_from_cursor();
        update_status_for_panel(panel_index);
        if (finished_cb) {
          finished_cb(true);
        }
      },
      false,
      {},
      panel.archive.current_token,
      panel.archive_display_source());
}

bool MainWindow::reload_archive_virtual_directory_for_panel(int panel_index) {
  return reload_archive_virtual_directory_for_panel(panel_index, {});
}

void MainWindow::reload_archive_virtual_directories_serially(
    QVector<int> panel_indexes,
    const std::function<void()>& finished_cb) {
  panel_indexes.erase(
      std::remove_if(panel_indexes.begin(),
                     panel_indexes.end(),
                     [this](int panel_index) {
                       return !in_archive_view_for_panel(panel_index);
                     }),
      panel_indexes.end());
  if (panel_indexes.isEmpty()) {
    if (finished_cb) {
      finished_cb();
    }
    return;
  }

  struct ArchiveReloadQueue final
      : public std::enable_shared_from_this<ArchiveReloadQueue> {
    QPointer<MainWindow> owner;
    QVector<int> pending_panels;
    std::function<void()> finished;

    void start_next() {
      if (owner.isNull() || pending_panels.isEmpty()) {
        if (finished) {
          finished();
        }
        return;
      }

      const int panel_index = pending_panels.front();
      pending_panels.removeFirst();
      MainWindow* const window = owner.data();
      const bool started = window->reload_archive_virtual_directory_for_panel(
          panel_index,
          [self = shared_from_this()](bool) { self->start_next(); });
      if (!started) {
        start_next();
      }
    }
  };

  auto queue = std::make_shared<ArchiveReloadQueue>();
  queue->owner = this;
  queue->pending_panels = std::move(panel_indexes);
  queue->finished = finished_cb;
  queue->start_next();
}

void MainWindow::clear_archive_view() {
  close_archive_view_for_panel(active_panel_index_);
}

bool MainWindow::archive_session_token_referenced_outside_panel(
    int panel_index,
    z7::app::ArchiveSessionToken token) const {
  if (!token.is_valid()) {
    return false;
  }

  for (int i = 0; i < 2; ++i) {
    if (i == panel_index) {
      continue;
    }
    const PanelController& panel = panel_controller(i);
    if (!panel.in_archive_view()) {
      continue;
    }
    if (panel.archive.current_token == token) {
      return true;
    }
    for (const PanelController::ArchiveState::ParentContext& parent :
         panel.archive.parent_stack) {
      if (parent.session_token == token) {
        return true;
      }
    }
  }
  return false;
}

QVector<z7::app::ArchiveSessionToken>
MainWindow::filter_archive_session_tokens_for_panel_close(
    int panel_index,
    QVector<z7::app::ArchiveSessionToken> tokens) const {
  tokens.erase(
      std::remove_if(
          tokens.begin(),
          tokens.end(),
          [this, panel_index](z7::app::ArchiveSessionToken token) {
            return !token.is_valid() ||
                   archive_session_token_referenced_outside_panel(panel_index,
                                                                  token);
          }),
      tokens.end());
  return tokens;
}

bool MainWindow::archive_temp_session_referenced_outside_panel(
    int panel_index,
    const QSharedPointer<ArchiveTempSession>& session) const {
  if (session == nullptr) {
    return false;
  }

  for (int i = 0; i < 2; ++i) {
    if (i == panel_index) {
      continue;
    }
    const PanelController& panel = panel_controller(i);
    if (!panel.in_archive_view()) {
      continue;
    }
    if (panel.archive.temp_session == session) {
      return true;
    }
    for (const PanelController::ArchiveState::ParentContext& parent :
         panel.archive.parent_stack) {
      if (parent.temp_session == session) {
        return true;
      }
    }
  }
  return false;
}

void MainWindow::release_archive_temp_session_for_panel_close(
    int panel_index,
    const QSharedPointer<ArchiveTempSession>& session) {
  if (archive_temp_session_referenced_outside_panel(panel_index, session)) {
    return;
  }
  release_archive_temp_session(session);
}

void MainWindow::close_archive_view_for_panel(
    int panel_index,
    const std::function<void(bool)>& finished_cb) {
  if (!in_archive_view_for_panel(panel_index)) {
    if (finished_cb) {
      finished_cb(true);
    }
    return;
  }

  const PanelController& panel = panel_controller(panel_index);
  QVector<z7::app::ArchiveSessionToken> tokens;
  if (panel.archive.current_token.is_valid()) {
    tokens.push_back(panel.archive.current_token);
  }
  for (auto it = panel.archive.parent_stack.rbegin();
       it != panel.archive.parent_stack.rend();
       ++it) {
    if (it->session_token.is_valid()) {
      tokens.push_back(it->session_token);
    }
  }
  tokens = filter_archive_session_tokens_for_panel_close(panel_index,
                                                         std::move(tokens));

  close_archive_sessions_async(
      std::move(tokens),
      [this, panel_index, finished_cb](bool ok) {
        if (!ok) {
          if (finished_cb) {
            finished_cb(false);
          }
          return;
        }

        panel_controller(panel_index).clear_archive_view_state(
            [this, panel_index](
                const QSharedPointer<ArchiveTempSession>& session) {
              release_archive_temp_session_for_panel_close(panel_index,
                                                           session);
            });
        apply_archive_preview_columns_visibility_for_panel(panel_index);
        if (panel_index == active_panel_index_) {
          refresh_active_panel_chrome();
        } else {
          update_status_for_panel(panel_index);
        }
        if (finished_cb) {
          finished_cb(true);
        }
      });
}

void MainWindow::close_archive_sessions_async(
    QVector<z7::app::ArchiveSessionToken> tokens,
    const std::function<void(bool)>& finished_cb) {
  QSet<quint64> seen;
  for (int i = tokens.size() - 1; i >= 0; --i) {
    const z7::app::ArchiveSessionToken token = tokens[i];
    if (!token.is_valid() || seen.contains(token.value)) {
      tokens.remove(i);
      continue;
    }
    seen.insert(token.value);
  }
  if (tokens.isEmpty()) {
    if (finished_cb) {
      finished_cb(true);
    }
    return;
  }

  auto remaining = std::make_shared<QVector<z7::app::ArchiveSessionToken>>(
      std::move(tokens));
  auto done = std::make_shared<std::function<void(bool)>>(finished_cb);
  auto start_next = std::make_shared<std::function<void()>>();
  *start_next = [this, remaining, start_next, done]() {
    while (!remaining->isEmpty() && !remaining->first().is_valid()) {
      remaining->remove(0);
    }
    if (remaining->isEmpty()) {
      if (*done) {
        (*done)(true);
      }
      return;
    }

    const z7::app::ArchiveSessionToken token = remaining->first();
    remaining->remove(0);
    const bool started = start_task_with_runner(
        QStringLiteral("Close archive session"),
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
        [token](ArchiveProcessRunner* runner) {
          return runner != nullptr &&
                 runner->start_close_session(token);
        },
        [start_next,
         done](bool ok,
               int,
               int,
               const QString&,
               const z7::app::OperationOutcome&) {
          if (!ok) {
            if (*done) {
              (*done)(false);
            }
            return;
          }
          (*start_next)();
        },
        RunnerTaskUiMode::kSilent);
    if (!started) {
      if (*done) {
        (*done)(false);
      }
    }
  };
  (*start_next)();
}

}  // namespace z7::ui::filemanager
