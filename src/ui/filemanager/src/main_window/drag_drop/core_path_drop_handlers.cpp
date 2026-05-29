// src/ui/filemanager/src/main_window/drag_drop/core_path_drop_handlers.cpp
// Role: Drag/drop handlers invoked from MainWindow::eventFilter.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "drag_source_marker.h"
#include "drop_effect_feedback.h"
#include "drop_logic.h"

namespace z7::ui::filemanager {

namespace {

QString classify_archive_drag_outcome(bool internal_archive_drop_handled,
                                      bool archive_transfer_requested,
                                      Qt::DropAction result_action,
                                      bool native_archive_drag,
                                      bool native_drag_session_started,
                                      bool native_ended_in_source_view,
                                      bool native_timed_out,
                                      int native_drag_item_count,
                                      int direct_export_attempts,
                                      int direct_export_successes,
                                      int promise_write_requests,
                                      int promise_write_finishes,
                                      const QString& error_message) {
  const bool has_error = !error_message.trimmed().isEmpty();
  if (internal_archive_drop_handled) {
    return QStringLiteral("Accepted");
  }
  if (has_error && !native_archive_drag) {
    return QStringLiteral("MaterializationFailed");
  }
  if (!native_archive_drag && result_action != Qt::IgnoreAction) {
    return QStringLiteral("Accepted");
  }
  if (native_archive_drag && !native_drag_session_started) {
    return QStringLiteral("NativeStartFailed");
  }

  const int expected_item_count = qMax(native_drag_item_count, 0);
  if (native_archive_drag && expected_item_count > 0 &&
      direct_export_successes == expected_item_count &&
      promise_write_requests == expected_item_count &&
      promise_write_finishes == expected_item_count) {
    return QStringLiteral("Accepted");
  }

  if (native_archive_drag &&
      native_ended_in_source_view &&
      !archive_transfer_requested &&
      !internal_archive_drop_handled &&
      promise_write_requests == 0) {
    return QStringLiteral("IgnoredSourceViewNoOp");
  }

  if (native_archive_drag && promise_write_requests == 0) {
    return QStringLiteral("TargetDidNotRequestPromise");
  }

  if (direct_export_attempts > 0 &&
      direct_export_successes < expected_item_count) {
    return QStringLiteral("DirectExportFailed");
  }

  if (native_archive_drag &&
      (promise_write_requests < expected_item_count ||
       promise_write_finishes < expected_item_count)) {
    return QStringLiteral("PromiseRequestedButNoWrite");
  }

  if (native_timed_out) {
    return QStringLiteral("SessionTimeout");
  }

  if (has_error) {
    return QStringLiteral("DirectExportFailed");
  }
  return QStringLiteral("Canceled");
}

QString archive_drag_warning_text(const QString& failure_class,
                                  const QString& error_message) {
  QString warning_text;
  if (failure_class == QStringLiteral("NativeStartFailed")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.native_start_failed"));
  } else if (failure_class == QStringLiteral("SessionTimeout")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.session_timeout"));
  } else if (failure_class == QStringLiteral("TargetDidNotRequestPromise")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.target_did_not_request_promise"));
  } else if (failure_class == QStringLiteral("PromiseRequestedButNoWrite")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.promise_requested_but_no_write"));
  } else if (failure_class == QStringLiteral("DirectExportFailed")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.direct_export_failed"));
  } else if (failure_class == QStringLiteral("MaterializationFailed")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.materialization_failed"));
  } else if (failure_class == QStringLiteral("IgnoredSourceViewNoOp")) {
    warning_text = QString();
  } else if (failure_class == QStringLiteral("Canceled")) {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.canceled"));
  } else {
    warning_text = z7::ui::runtime_support::J(
        QStringLiteral("ui.drag_drop.archive_warning.not_accepted"));
  }

  if (!error_message.trimmed().isEmpty()) {
    warning_text += QStringLiteral("\n\n");
    warning_text += error_message.trimmed();
  }
  return warning_text;
}

}  // namespace

QAbstractItemView* MainWindow::drop_target_view_for_panel(
    const PanelController& panel,
    const QObject* watched) const {
  QAbstractItemView* target_view = panel.current_item_view();
  if (watched == panel.ui.details_view || watched == panel.ui.details_view->viewport()) {
    target_view = panel.ui.details_view;
  } else if (watched == panel.ui.icon_list_view ||
             watched == panel.ui.icon_list_view->viewport()) {
    target_view = panel.ui.icon_list_view;
  }
  return target_view;
}

void MainWindow::on_panel_drag_finished(const DragExecutionReport& report) {
  if (report.native_filesystem_drag) {
    return;
  }

  if (!report.archive_source) {
    return;
  }

  const QString native_error_message = report.native_error_message.trimmed();
  const QString materialization_error_message =
      report.materialization_error_message.trimmed();
  const QString error_message =
      !materialization_error_message.isEmpty()
          ? materialization_error_message
          : native_error_message;

  const QString failure_class = classify_archive_drag_outcome(
      report.internal_archive_drop_handled,
      report.archive_transfer_requested,
      report.result_action,
      report.native_archive_drag,
      report.native_drag_session_started,
      report.native_ended_in_source_view,
      report.native_timed_out,
      report.native_drag_item_count,
      report.direct_export_attempts,
      report.direct_export_successes,
      report.promise_write_requests,
      report.promise_write_finishes,
      error_message);

#ifdef Z7_TESTING
  setProperty("z7.fm.drag.archive.last.result_action",
              static_cast<int>(report.result_action));
  setProperty("z7.fm.drag.archive.last.transfer_requested",
              report.archive_transfer_requested);
  setProperty("z7.fm.drag.archive.last.internal_drop_handled",
              report.internal_archive_drop_handled);
  setProperty("z7.fm.drag.archive.last.native_drag",
              report.native_archive_drag);
  setProperty("z7.fm.drag.archive.last.native_source_widget_resolved",
              report.native_source_widget_resolved);
  setProperty("z7.fm.drag.archive.last.native_view_available",
              report.native_view_available);
  setProperty("z7.fm.drag.archive.last.native_current_event_available",
              report.native_current_event_available);
  setProperty("z7.fm.drag.archive.last.native_drag_session_started",
              report.native_drag_session_started);
  setProperty("z7.fm.drag.archive.last.native_drag_completed",
              report.native_drag_completed);
  setProperty("z7.fm.drag.archive.last.native_ended_in_source_view",
              report.native_ended_in_source_view);
  setProperty("z7.fm.drag.archive.last.native_promise_writes_settled",
              report.native_promise_writes_settled);
  setProperty("z7.fm.drag.archive.last.native_timed_out",
              report.native_timed_out);
  setProperty("z7.fm.drag.archive.last.native_drag_item_count",
              report.native_drag_item_count);
  setProperty("z7.fm.drag.archive.last.promise_write_requests",
              report.promise_write_requests);
  setProperty("z7.fm.drag.archive.last.promise_write_finishes",
              report.promise_write_finishes);
  setProperty("z7.fm.drag.archive.last.promise_write_successes",
              report.promise_write_successes);
  setProperty("z7.fm.drag.archive.last.direct_export_attempts",
              report.direct_export_attempts);
  setProperty("z7.fm.drag.archive.last.direct_export_successes",
              report.direct_export_successes);
  setProperty("z7.fm.drag.archive.last.failure_class",
              failure_class);
  setProperty("z7.fm.drag.archive.last.error_message",
              error_message);
  setProperty("z7.fm.drag.archive.last.materialization_error_message",
              materialization_error_message);
#endif

  bool strict_failure =
      z7::platform::qt::mac_archive_drag_strict_failure_enabled();
  if (!strict_failure) {
    return;
  }

  if (failure_class == QStringLiteral("Accepted")) {
    return;
  }
  if (failure_class == QStringLiteral("IgnoredSourceViewNoOp")) {
    return;
  }

  QMessageBox::warning(
      this,
      QStringLiteral("7-Zip"),
      archive_drag_warning_text(failure_class, error_message));
}

MainWindow::DropSourceState MainWindow::resolve_drop_source_state(
    int panel_index,
    bool window_drop_target,
    const QDropEvent* drop_event,
    const QStringList& dropped_paths,
    const QString& drop_target_directory) const {
  DropSourceState state;
  Q_UNUSED(window_drop_target);
  if (drop_event == nullptr) {
    return state;
  }

  state.source_panel_index = panel_index_for_view(drop_event->source());
#ifdef Z7_TESTING
  const QVariant source_panel_override =
      property("z7.fm.drop.source.panel_index.override");
  if (source_panel_override.isValid()) {
    bool ok = false;
    const int override_panel = source_panel_override.toInt(&ok);
    if (ok) {
      state.source_panel_index = override_panel;
    }
  }
#endif
  const bool trusted_internal_fs_marker =
      is_trusted_internal_fs_source_marker(drop_event->mimeData());
  state.same_panel_source =
      !window_drop_target &&
      state.source_panel_index >= 0 &&
      state.source_panel_index == panel_index;
  state.internal_fs_source =
      state.source_panel_index >= 0 &&
      state.source_panel_index != panel_index &&
      !in_archive_view_for_panel(state.source_panel_index);
  if (!state.internal_fs_source &&
      has_internal_fs_source_marker(drop_event->mimeData())) {
    state.internal_fs_source = true;
  }
#ifdef Z7_TESTING
  const QVariant internal_fs_override =
      property("z7.fm.drop.source.internal_fs.override");
  if (internal_fs_override.isValid()) {
    state.internal_fs_source = internal_fs_override.toBool();
  }
#endif

  state.trusted_internal_fs_source =
      state.source_panel_index >= 0 &&
      state.source_panel_index != panel_index &&
      !in_archive_view_for_panel(state.source_panel_index);
  if (!state.trusted_internal_fs_source &&
      trusted_internal_fs_marker) {
    state.trusted_internal_fs_source = true;
  }
#ifdef Z7_TESTING
  const QVariant trusted_internal_override =
      property("z7.fm.drop.source.trusted_internal.override");
  if (trusted_internal_override.isValid()) {
    state.trusted_internal_fs_source = trusted_internal_override.toBool();
  }
#endif
  if (!state.internal_fs_source) {
    state.trusted_internal_fs_source = false;
  }

  InternalArchiveSourcePayload archive_payload;
  bool archive_marker_trusted = false;
  if (read_internal_archive_source_marker(drop_event->mimeData(),
                                          &archive_payload,
                                          &archive_marker_trusted) &&
      archive_marker_trusted) {
    state.internal_archive_source = true;
    state.trusted_internal_archive_source = true;
    state.archive_source_path = archive_payload.archive_path;
    state.archive_source_type_hint = archive_payload.archive_type_hint;
    state.archive_source_entries = archive_payload.entries;
    state.archive_source_session_token.value = archive_payload.session_token_value;
  }

  SourceTargetVolumeRelation volume_relation =
      SourceTargetVolumeRelation::kUnknown;
  volume_relation = source_target_volume_relation(dropped_paths,
                                                  drop_target_directory);
#ifdef Z7_TESTING
  SourceTargetVolumeRelation override_relation =
      SourceTargetVolumeRelation::kUnknown;
  if (parse_volume_relation_override(
          property("z7.fm.drop.source.volume_relation.override"),
          &override_relation)) {
    volume_relation = override_relation;
  }
#endif
  state.source_target_same_volume =
      source_target_all_on_same_volume(volume_relation);
#ifdef Z7_TESTING
  const QVariant same_volume_override =
      property("z7.fm.drop.source.same_volume.override");
  if (same_volume_override.isValid()) {
    state.source_target_same_volume = same_volume_override.toBool();
  }
#endif

  return state;
}

bool MainWindow::handle_panel_drag_enter_or_move(QObject* watched,
                                                 int panel_index,
                                                 bool window_drop_target,
                                                 QDropEvent* drop_like_event) {
  if (drop_like_event == nullptr) {
    return false;
  }

  InternalArchiveSourcePayload archive_payload;
  bool archive_marker_trusted = false;
  const bool has_trusted_archive_source =
      read_internal_archive_source_marker(drop_like_event->mimeData(),
                                          &archive_payload,
                                          &archive_marker_trusted) &&
      archive_marker_trusted &&
      !archive_payload.entries.isEmpty();
  const QStringList dropped_paths = has_trusted_archive_source
                                        ? QStringList()
                                        : local_existing_drop_paths(
                                              drop_like_event->mimeData());
  if (dropped_paths.isEmpty() && !has_trusted_archive_source) {
    return false;
  }

  const bool archive_view = in_archive_view_for_panel(panel_index);
  const PanelController& panel = panel_controller(panel_index);
  QAbstractItemView* target_view =
      drop_target_view_for_panel(panel, watched);
  const QString panel_fs_directory =
      panel.model != nullptr ? panel.model->directory() : QString();
  const DropTargetInfo drop_target = resolve_drop_target_info_for_panel(
      panel_index, target_view, watched, drop_like_event, panel_fs_directory);

  const DropSourceState source_state = resolve_drop_source_state(
      panel_index,
      window_drop_target,
      drop_like_event,
      dropped_paths,
      drop_target.directory);
  if (source_state.same_panel_source) {
    drop_like_event->setDropAction(Qt::IgnoreAction);
    drop_like_event->ignore();
    return true;
  }

  const DropCommand preview_command =
      choose_drop_preview_command(this,
                                  archive_view,
                                  drop_target.allow_copy_move,
                                  window_drop_target,
                                  source_state.internal_fs_source,
                                  source_state.internal_archive_source,
                                  source_state.source_target_same_volume,
                                  drop_like_event);
  if (preview_command == DropCommand::kCancel) {
    drop_like_event->setDropAction(Qt::IgnoreAction);
    drop_like_event->ignore();
    return true;
  }

  set_active_panel(panel_index);
  drop_like_event->setDropAction(drop_action_for_command(preview_command));
  drop_like_event->accept();
  return true;
}

}  // namespace z7::ui::filemanager
