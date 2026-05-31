// src/ui/filemanager/src/main_window/drag_drop/drag_aware_views.cpp
// Role: Start-drag wrappers that emit archive drag completion diagnostics.

#include "drag_aware_views.h"

#include "main_window/model/model.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDrag>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

#include <memory>
#include <limits>

#include "drag_drop_policy_qt.h"
#include "structured_list_proxy.h"

#if defined(Q_OS_MAC)
#include "macos_native_drag.h"
#endif

namespace z7::ui::filemanager {

namespace {

#ifdef Z7_TESTING
constexpr const char* kShiftSelectionQueryModifiersOverrideProperty =
    "z7.fm.shift_selection.query_modifiers.override";
constexpr const char* kShiftSelectionLastMousePressProperty =
    "z7.fm.shift_selection.last_mouse_press";
constexpr const char* kShiftSelectionLastMouseReleaseProperty =
    "z7.fm.shift_selection.last_mouse_release";
#endif

struct MouseShiftModifierState {
  bool event_shift = false;
  bool view_shift = false;
  bool application_shift = false;
  bool query_shift = false;

  bool active() const {
    return event_shift || view_shift || application_shift || query_shift;
  }
};

void reset_mouse_shift_anchor(QPersistentModelIndex* anchor,
                              QPointer<QAbstractItemModel>* anchor_model,
                              int* anchor_row) {
  if (anchor != nullptr) {
    *anchor = QPersistentModelIndex();
  }
  if (anchor_model != nullptr) {
    *anchor_model = nullptr;
  }
  if (anchor_row != nullptr) {
    *anchor_row = -1;
  }
}

void set_mouse_shift_anchor(const QModelIndex& anchor_index,
                            QPersistentModelIndex* anchor,
                            QPointer<QAbstractItemModel>* anchor_model,
                            int* anchor_row) {
  if (anchor != nullptr) {
    *anchor = anchor_index;
  }
  if (anchor_model != nullptr) {
    *anchor_model = const_cast<QAbstractItemModel*>(anchor_index.model());
  }
  if (anchor_row != nullptr) {
    *anchor_row = anchor_index.isValid() ? anchor_index.row() : -1;
  }
}

QModelIndex mouse_shift_anchor_index(
    QAbstractItemView* view,
    const QModelIndex& target,
    int selection_column,
    const QPersistentModelIndex& persistent_anchor,
    const QPointer<QAbstractItemModel>& anchor_model,
    int anchor_row) {
  if (view == nullptr || view->model() == nullptr || !target.isValid()) {
    return {};
  }

  if (!anchor_model.isNull() && anchor_model == view->model() &&
      anchor_row >= 0 &&
      anchor_row < view->model()->rowCount(target.parent())) {
    return view->model()->index(anchor_row, selection_column, target.parent());
  }

  const QModelIndex persistent = persistent_anchor;
  if (persistent.isValid() && persistent.model() == target.model()) {
    if (persistent.column() == selection_column) {
      return persistent;
    }
    return view->model()->index(persistent.row(),
                                selection_column,
                                persistent.parent());
  }

  return {};
}

int mouse_shift_anchor_row_for_diagnostic(
    QAbstractItemView* view,
    const QPersistentModelIndex& persistent_anchor,
    const QPointer<QAbstractItemModel>& anchor_model,
    int anchor_row) {
  if (view != nullptr && !anchor_model.isNull() &&
      anchor_model == view->model() && anchor_row >= 0) {
    return anchor_row;
  }
  const QModelIndex persistent = persistent_anchor;
  return persistent.isValid() ? persistent.row() : -1;
}

QModelIndex selection_column_index(QAbstractItemView* view,
                                   const QModelIndex& index,
                                   int selection_column) {
  if (view == nullptr || view->model() == nullptr || !index.isValid()) {
    return {};
  }
  if (index.column() == selection_column) {
    return index;
  }
  return view->model()->index(index.row(), selection_column, index.parent());
}

QModelIndex linear_shift_target_for_key(QAbstractItemView* view,
                                        int key,
                                        int selection_column) {
  if (view == nullptr || view->model() == nullptr ||
      view->selectionModel() == nullptr) {
    return {};
  }

  const QModelIndex raw_current =
      view->currentIndex().isValid() ? view->currentIndex()
                                     : view->selectionModel()->currentIndex();
  const QModelIndex current =
      selection_column_index(view, raw_current, selection_column);
  if (!current.isValid()) {
    return {};
  }

  int next_row = current.row();
  if (key == Qt::Key_Up || key == Qt::Key_Left) {
    --next_row;
  } else if (key == Qt::Key_Down || key == Qt::Key_Right) {
    ++next_row;
  }
  if (next_row < 0 || next_row >= view->model()->rowCount(current.parent())) {
    return current;
  }
  return view->model()->index(next_row, selection_column, current.parent());
}

bool is_parent_link_index(const QModelIndex& index) {
  if (!index.isValid()) {
    return false;
  }

  const QAbstractItemModel* model = index.model();
  QModelIndex source_index = index;
  if (const auto* proxy =
          qobject_cast<const z7::ui::widgets::StructuredListSortFilterProxy*>(
              model);
      proxy != nullptr) {
    source_index = proxy->mapToSource(index);
    model = proxy->sourceModel();
  }

  const auto* directory_model = dynamic_cast<const DirectoryListModel*>(model);
  return directory_model != nullptr &&
         directory_model->is_parent_link_for_row(source_index.row());
}

bool selection_only_contains_row(const QItemSelectionModel* selection,
                                 int row) {
  if (selection == nullptr || row < 0) {
    return false;
  }
  const QModelIndexList selected = selection->selectedIndexes();
  if (selected.isEmpty()) {
    return false;
  }
  for (const QModelIndex& index : selected) {
    if (!index.isValid() || index.row() != row) {
      return false;
    }
  }
  return true;
}

void set_primary_item_selected(QItemSelectionModel* selection,
                               const QModelIndex& primary,
                               bool selected) {
  if (selection == nullptr || !primary.isValid()) {
    return;
  }

  if (selected) {
    selection->select(primary, QItemSelectionModel::Select);
    return;
  }

  const int last_column = primary.model() != nullptr
                              ? primary.model()->columnCount(primary.parent()) - 1
                              : primary.column();
  const QModelIndex row_start =
      primary.model()->index(primary.row(), 0, primary.parent());
  const QModelIndex row_end =
      primary.model()->index(primary.row(), qMax(0, last_column), primary.parent());
  selection->select(QItemSelection(row_start, row_end),
                    QItemSelectionModel::Deselect);
}

bool apply_standard_shift_selection(QAbstractItemView* view,
                                    const QModelIndex& current,
                                    const QModelIndex& target,
                                    int selection_column,
                                    QPersistentModelIndex* anchor) {
  if (view == nullptr || view->model() == nullptr || anchor == nullptr) {
    return false;
  }
  QItemSelectionModel* selection = view->selectionModel();
  if (selection == nullptr || !current.isValid() || !target.isValid()) {
    return false;
  }

  QModelIndex range_anchor = *anchor;
  if (!range_anchor.isValid() || range_anchor.model() != target.model()) {
    range_anchor = current;
    *anchor = range_anchor;
  }

  const int top = qMin(range_anchor.row(), target.row());
  const int bottom = qMax(range_anchor.row(), target.row());
  const QModelIndex top_index =
      view->model()->index(top, selection_column, target.parent());
  const QModelIndex bottom_index =
      view->model()->index(bottom, selection_column, target.parent());
  if (!top_index.isValid() || !bottom_index.isValid()) {
    return false;
  }

  selection->select(QItemSelection(top_index, bottom_index),
                    QItemSelectionModel::ClearAndSelect);
  selection->setCurrentIndex(target, QItemSelectionModel::NoUpdate);
  view->scrollTo(target);
  return true;
}

bool apply_alternative_shift_selection(QAbstractItemView* view,
                                       const QModelIndex& current,
                                       const QModelIndex& target,
                                       int selection_column,
                                       bool* selection_defined,
                                       bool* select_mark) {
  if (view == nullptr || view->model() == nullptr ||
      selection_defined == nullptr || select_mark == nullptr) {
    return false;
  }
  QItemSelectionModel* selection = view->selectionModel();
  if (selection == nullptr || !current.isValid() || !target.isValid()) {
    return false;
  }

  if (!*selection_defined) {
    *selection_defined = true;
    const bool focus_only_selection =
        selection->isSelected(current) &&
        selection_only_contains_row(selection, current.row());
    *select_mark = is_parent_link_index(current) ||
                   !selection->isSelected(current) ||
                   focus_only_selection;
  }

  const int top = qMin(current.row(), target.row());
  const int bottom = qMax(current.row(), target.row());
  for (int row = top; row <= bottom; ++row) {
    const QModelIndex primary =
        view->model()->index(row, selection_column, target.parent());
    if (!primary.isValid()) {
      continue;
    }
    if (is_parent_link_index(primary)) {
      set_primary_item_selected(selection, primary, false);
      continue;
    }
    set_primary_item_selected(selection, primary, *select_mark);
  }

  selection->setCurrentIndex(target, QItemSelectionModel::NoUpdate);
  view->scrollTo(target);
  return true;
}

bool apply_keyboard_shift_selection(QAbstractItemView* view,
                                    const QModelIndex& raw_target,
                                    int selection_column,
                                    bool alternative_selection_mode,
                                    QPersistentModelIndex* anchor,
                                    bool* alternative_selection_defined,
                                    bool* alternative_select_mark) {
  if (view == nullptr || view->model() == nullptr) {
    return false;
  }

  QItemSelectionModel* selection = view->selectionModel();
  if (selection == nullptr) {
    return false;
  }

  const QModelIndex raw_current =
      view->currentIndex().isValid() ? view->currentIndex()
                                     : selection->currentIndex();
  const QModelIndex current =
      selection_column_index(view, raw_current, selection_column);
  QModelIndex target = selection_column_index(view, raw_target, selection_column);
  if (!target.isValid()) {
    target = current;
  }
  if (!current.isValid() || !target.isValid()) {
    return false;
  }

  if (alternative_selection_mode) {
    return apply_alternative_shift_selection(view,
                                             current,
                                             target,
                                             selection_column,
                                             alternative_selection_defined,
                                             alternative_select_mark);
  }
  return apply_standard_shift_selection(view,
                                        current,
                                        target,
                                        selection_column,
                                        anchor);
}

bool apply_mouse_shift_click_selection(QAbstractItemView* view,
                                       const QModelIndex& raw_target,
                                       int selection_column,
                                       QPersistentModelIndex* anchor,
                                       QPointer<QAbstractItemModel>* anchor_model,
                                       int* anchor_row,
                                       bool skip_parent_links) {
  if (view == nullptr || view->model() == nullptr || anchor == nullptr ||
      anchor_model == nullptr || anchor_row == nullptr) {
    return false;
  }
  QItemSelectionModel* selection = view->selectionModel();
  if (selection == nullptr) {
    return false;
  }

  const QModelIndex target =
      selection_column_index(view, raw_target, selection_column);
  if (!target.isValid()) {
    return false;
  }

  QModelIndex range_anchor = mouse_shift_anchor_index(view,
                                                      target,
                                                      selection_column,
                                                      *anchor,
                                                      *anchor_model,
                                                      *anchor_row);
  if (!range_anchor.isValid()) {
    range_anchor = target;
    set_mouse_shift_anchor(range_anchor, anchor, anchor_model, anchor_row);
  }

  selection->clearSelection();
  const int top = qMin(range_anchor.row(), target.row());
  const int bottom = qMax(range_anchor.row(), target.row());
  for (int row = top; row <= bottom; ++row) {
    const QModelIndex primary =
        view->model()->index(row, selection_column, target.parent());
    if (!primary.isValid() ||
        (skip_parent_links && is_parent_link_index(primary))) {
      continue;
    }
    selection->select(primary, QItemSelectionModel::Select);
  }
  selection->setCurrentIndex(target, QItemSelectionModel::NoUpdate);
  view->scrollTo(target);
  return true;
}

Qt::KeyboardModifiers query_keyboard_modifiers_for_shift_click(
    const QAbstractItemView* view) {
#ifdef Z7_TESTING
  if (view != nullptr) {
    const QVariant override_value =
        view->property(kShiftSelectionQueryModifiersOverrideProperty);
    if (override_value.isValid()) {
      return Qt::KeyboardModifiers(
          Qt::KeyboardModifier(override_value.toInt()));
    }
  }
#else
  Q_UNUSED(view);
#endif
  return QGuiApplication::queryKeyboardModifiers();
}

MouseShiftModifierState mouse_shift_modifier_state(
    const QAbstractItemView* view,
    const QMouseEvent* event,
    bool keyboard_shift_pressed) {
  MouseShiftModifierState state;
  if (event != nullptr) {
    state.event_shift = event->modifiers().testFlag(Qt::ShiftModifier);
  }
  state.view_shift = keyboard_shift_pressed;
  state.application_shift =
      QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
  state.query_shift =
      query_keyboard_modifiers_for_shift_click(view).testFlag(
          Qt::ShiftModifier);
  return state;
}

#ifdef Z7_TESTING
int selected_row_count(const QAbstractItemView* view) {
  if (view == nullptr || view->selectionModel() == nullptr) {
    return 0;
  }

  QSet<int> rows;
  const QModelIndexList selected = view->selectionModel()->selectedIndexes();
  for (const QModelIndex& index : selected) {
    if (index.isValid()) {
      rows.insert(index.row());
    }
  }
  return rows.size();
}

void record_mouse_shift_press_diagnostic(QAbstractItemView* view,
                                         int hit_row,
                                         bool primary_hit,
                                         int anchor_row_before,
                                         int anchor_row_after,
                                         const MouseShiftModifierState& state,
                                         bool custom_shift_click,
                                         const QString& fallback_reason) {
  if (view == nullptr) {
    return;
  }

  QVariantMap diagnostic;
  diagnostic.insert(QStringLiteral("hitRow"), hit_row);
  diagnostic.insert(QStringLiteral("primaryHit"), primary_hit);
  diagnostic.insert(QStringLiteral("anchorRowBefore"), anchor_row_before);
  diagnostic.insert(QStringLiteral("anchorRowAfter"), anchor_row_after);
  diagnostic.insert(QStringLiteral("eventShift"), state.event_shift);
  diagnostic.insert(QStringLiteral("viewShift"), state.view_shift);
  diagnostic.insert(QStringLiteral("applicationShift"),
                    state.application_shift);
  diagnostic.insert(QStringLiteral("queryShift"), state.query_shift);
  diagnostic.insert(QStringLiteral("shiftActive"), state.active());
  diagnostic.insert(QStringLiteral("customShiftClick"), custom_shift_click);
  const int selected_rows = selected_row_count(view);
  diagnostic.insert(QStringLiteral("selectionRowCountAfter"), selected_rows);
  diagnostic.insert(QStringLiteral("fallbackReason"), fallback_reason);
  view->setProperty(kShiftSelectionLastMousePressProperty, diagnostic);
}

void record_mouse_shift_release_diagnostic(QAbstractItemView* view,
                                           bool consumed_release) {
  if (view == nullptr) {
    return;
  }

  QVariantMap diagnostic;
  diagnostic.insert(QStringLiteral("consumedRelease"), consumed_release);
  const int selected_rows = selected_row_count(view);
  diagnostic.insert(QStringLiteral("selectionRowCountAfter"), selected_rows);
  view->setProperty(kShiftSelectionLastMouseReleaseProperty, diagnostic);
}
#endif

QModelIndexList selected_drag_indexes(const QAbstractItemView* view) {
  if (view == nullptr || view->selectionModel() == nullptr) {
    return {};
  }

  QModelIndexList indexes = view->selectionModel()->selectedIndexes();
  if (indexes.isEmpty()) {
    const QModelIndex current = view->currentIndex();
    if (current.isValid()) {
      indexes.push_back(current);
    }
  }
  return indexes;
}

Qt::DropAction choose_default_drag_action(Qt::DropActions supported_actions) {
  // A generic Qt drag source cannot know whether an external target is on the
  // same volume or will complete a move safely. Prefer Copy here; internal
  // panel drops still compute their own target-side Move/Copy default.
  if (supported_actions.testFlag(Qt::CopyAction)) {
    return Qt::CopyAction;
  }
  if (supported_actions.testFlag(Qt::MoveAction)) {
    return Qt::MoveAction;
  }
  if (supported_actions.testFlag(Qt::LinkAction)) {
    return Qt::LinkAction;
  }
  return Qt::IgnoreAction;
}

DragExecutionReport build_drag_report(const QMimeData* mime_data,
                                      Qt::DropAction result_action) {
  DragExecutionReport report;
  report.result_action = result_action;
  if (mime_data == nullptr) {
    return report;
  }

  report.archive_source =
      mime_data->hasFormat(QString::fromLatin1(kMimeTypeZ7FmArchiveSource));
  report.archive_transfer_requested =
      mime_data->hasFormat(QString::fromLatin1(kMimeTypeZ7FmArchiveTransferRequested));
  report.internal_archive_drop_handled =
      mime_data->hasFormat(QString::fromLatin1(kMimeTypeZ7FmArchiveInternalDropHandled));
  if (mime_data->hasFormat(
          QString::fromLatin1(kMimeTypeZ7FmArchiveMaterializationError))) {
    report.materialization_error_message =
        QString::fromUtf8(mime_data->data(
            QString::fromLatin1(kMimeTypeZ7FmArchiveMaterializationError)))
            .trimmed();
  }
  return report;
}

#if defined(Q_OS_MAC)
QString archive_drag_display_name(const QString& archive_entry) {
  QString normalized_entry = QDir::fromNativeSeparators(archive_entry.trimmed());
  while (normalized_entry.endsWith(QLatin1Char('/'))) {
    normalized_entry.chop(1);
  }

  const QString file_name = QFileInfo(normalized_entry).fileName().trimmed();
  if (!file_name.isEmpty()) {
    return file_name;
  }
  return normalized_entry.isEmpty()
             ? QStringLiteral("7zFM-item")
             : normalized_entry;
}

QHash<QString, bool> archive_drag_directory_flags(const QModelIndexList& indexes) {
  QHash<QString, bool> out;
  for (const QModelIndex& index : indexes) {
    if (!index.isValid()) {
      continue;
    }

    const QString entry = index.data(Qt::UserRole).toString().trimmed();
    if (entry.isEmpty()) {
      continue;
    }

    bool ok = false;
    const int sort_group =
        index.data(z7::ui::widgets::StructuredListSortFilterProxy::kSortGroupRole)
            .toInt(&ok);
    const bool is_dir = ok && sort_group == 1;
    if (is_dir || !out.contains(entry)) {
      out.insert(entry, is_dir);
    }
  }
  return out;
}

qint64 archive_drag_estimated_payload_bytes_for_row(const QModelIndex& index) {
  if (!index.isValid()) {
    return 0;
  }

  const QModelIndex size_index =
      index.sibling(index.row(), DirectoryListModel::kSizeColumn);
  bool ok = false;
  const qulonglong size_value =
      size_index.data(z7::ui::widgets::StructuredListSortFilterProxy::kSortKeyRole)
          .toULongLong(&ok);
  if (!ok) {
    return 0;
  }

  return static_cast<qint64>(qMin<qulonglong>(
      size_value, static_cast<qulonglong>(std::numeric_limits<qint64>::max())));
}

QHash<QString, qint64> archive_drag_payload_size_estimates(
    const QModelIndexList& indexes) {
  QHash<QString, qint64> out;
  QSet<int> seen_rows;
  for (const QModelIndex& index : indexes) {
    if (!index.isValid() || seen_rows.contains(index.row())) {
      continue;
    }
    seen_rows.insert(index.row());

    const QString entry = index.data(Qt::UserRole).toString().trimmed();
    if (entry.isEmpty()) {
      continue;
    }

    out.insert(entry, archive_drag_estimated_payload_bytes_for_row(index));
  }
  return out;
}

QVector<z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem>
filesystem_native_drag_items(const QModelIndexList& indexes) {
  QVector<z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem>
      out;
  QSet<QString> seen_paths;
  out.reserve(indexes.size());
  for (const QModelIndex& index : indexes) {
    if (!index.isValid()) {
      continue;
    }

    const QString source_path = index.data(Qt::UserRole).toString().trimmed();
    if (source_path.isEmpty() || seen_paths.contains(source_path)) {
      continue;
    }
    const QFileInfo source_info(source_path);
    if (!source_info.exists()) {
      continue;
    }
    seen_paths.insert(source_path);

    z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem item;
    item.source_path = source_info.absoluteFilePath();
    item.suggested_file_name = source_info.fileName();
    item.is_dir = source_info.isDir();
    item.estimate_payload_size_bytes = [source_info]() {
      return source_info.isDir() ? qint64{0}
                                 : qMax<qint64>(0, source_info.size());
    };
    out.push_back(std::move(item));
  }
  return out;
}

#endif

void finish_standard_drag(QPointer<QAbstractItemView> view,
                          Qt::DropActions supported_actions,
                          QMimeData* mime_data,
                          const std::function<void(const DragExecutionReport&)>& callback) {
  if (view.isNull() || mime_data == nullptr) {
    delete mime_data;
    return;
  }

  QDrag drag(view);
  drag.setMimeData(mime_data);
  const Qt::DropAction result_action =
      drag.exec(supported_actions, choose_default_drag_action(supported_actions));

  if (callback) {
    callback(build_drag_report(mime_data, result_action));
  }
}

void start_drag_with_callback(QAbstractItemView* view,
                              Qt::DropActions supported_actions,
                              const DragAwareTreeView::ArchiveDragMaterializer&
                                  archive_drag_materializer,
                              const DragAwareTreeView::ArchiveDragDirectExporter&
                                  archive_drag_direct_exporter,
                              const std::function<void(const DragExecutionReport&)>& callback) {
  if (view == nullptr || view->model() == nullptr) {
    return;
  }

  const QModelIndexList indexes = selected_drag_indexes(view);
  if (indexes.isEmpty()) {
    return;
  }

  QMimeData* mime_data = view->model()->mimeData(indexes);
  if (mime_data == nullptr) {
    return;
  }

  InternalArchiveSourcePayload archive_payload;
  bool trusted_archive_source = false;
  const bool has_archive_payload =
      read_internal_archive_source_marker(mime_data, &archive_payload, &trusted_archive_source) &&
      trusted_archive_source &&
      !archive_payload.entries.isEmpty() &&
      !archive_payload.archive_path.trimmed().isEmpty();

  DirectoryListModel::DataMode data_mode = DirectoryListModel::DataMode::kFilesystem;
  const QAbstractItemModel* model = view->model();
  if (const auto* proxy =
          qobject_cast<const z7::ui::widgets::StructuredListSortFilterProxy*>(model);
      proxy != nullptr) {
    model = proxy->sourceModel();
  }
  if (const auto* directory_model =
          dynamic_cast<const DirectoryListModel*>(model);
      directory_model != nullptr) {
    data_mode = directory_model->data_mode();
  }

#if defined(Q_OS_MAC)
  const bool has_filesystem_payload =
      !has_archive_payload &&
      data_mode == DirectoryListModel::DataMode::kFilesystem;
#endif

  if (!has_archive_payload || !archive_drag_materializer) {
#if defined(Q_OS_MAC)
    if (has_filesystem_payload) {
      QPointer<QAbstractItemView> view_ptr(view);
      if (!view_ptr.isNull()) {
        const auto items = filesystem_native_drag_items(indexes);
        if (!items.isEmpty()) {
          z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest
              native_request;
          native_request.source_widget = view_ptr;
          native_request.kind =
              z7::macos_integration::native_drag::MacOSIntegrationNativeDragKind::kFilesystem;
          native_request.supported_actions = supported_actions;
          native_request.items = items;
          const auto native_result =
              z7::macos_integration::native_drag::run_macos_integration_native_drag(
                  native_request);
          if (native_result.handled) {
            if (callback) {
              DragExecutionReport report;
              report.native_filesystem_drag = true;
              report.result_action = native_result.result_action;
              report.native_source_widget_resolved =
                  native_result.source_widget_resolved;
              report.native_view_available = native_result.native_view_available;
              report.native_current_event_available =
                  native_result.current_event_available;
              report.native_drag_session_started =
                  native_result.drag_session_started;
              report.native_drag_completed = native_result.drag_completed;
              report.native_ended_in_source_view =
                  native_result.ended_in_source_view;
              report.native_ended_in_own_app_window =
                  native_result.ended_in_own_app_window;
              report.native_promise_writes_settled =
                  native_result.promise_writes_settled;
              report.native_timed_out = native_result.timed_out;
              report.native_drag_item_count = native_result.drag_item_count;
              report.promise_write_requests =
                  native_result.promise_write_requests;
              report.promise_write_finishes =
                  native_result.promise_write_finishes;
              report.promise_write_successes =
                  native_result.promise_write_successes;
              report.direct_export_attempts =
                  native_result.direct_export_attempts;
              report.direct_export_successes =
                  native_result.direct_export_successes;
              report.native_error_message =
                  native_result.error_message.trimmed();
              callback(report);
            }
            delete mime_data;
            return;
          }
        }
      }
    }
#endif
    finish_standard_drag(view, supported_actions, mime_data, callback);
    return;
  }

  QPointer<QAbstractItemView> view_ptr(view);
#if defined(Q_OS_MAC)
  if (!archive_drag_direct_exporter) {
    finish_standard_drag(view, supported_actions, mime_data, callback);
    return;
  }

  QString native_error_message;
  z7::macos_integration::native_drag::MacOSIntegrationNativeDragResult
      native_result_snapshot;
  if (!view_ptr.isNull()) {
    const QHash<QString, bool> archive_entry_is_dir =
        archive_drag_directory_flags(indexes);
    const QHash<QString, qint64> archive_entry_payload_sizes =
        archive_drag_payload_size_estimates(indexes);
    z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest native_request;
    native_request.source_widget = view_ptr;
    native_request.kind =
        z7::macos_integration::native_drag::MacOSIntegrationNativeDragKind::kArchive;
    native_request.supported_actions = Qt::CopyAction;
    native_request.items.reserve(archive_payload.entries.size());
    for (const QString& entry : archive_payload.entries) {
      const QString normalized_entry = entry.trimmed();
      if (normalized_entry.isEmpty()) {
        continue;
      }

      z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem item;
      item.archive_entry_path = normalized_entry;
      item.suggested_file_name = archive_drag_display_name(normalized_entry);
      item.is_dir = archive_entry_is_dir.value(normalized_entry, false);
      item.write_to_destination =
          [archive_drag_direct_exporter,
           normalized_entry,
           is_dir = item.is_dir](const QString& destination_path,
                                 QString* error) {
            return archive_drag_direct_exporter(
                normalized_entry, is_dir, destination_path, error);
          };
      item.estimate_payload_size_bytes =
          [estimated_bytes = archive_entry_payload_sizes.value(normalized_entry, 0)]() {
            return estimated_bytes;
          };
      native_request.items.push_back(std::move(item));
    }

    if (!native_request.items.isEmpty()) {
      const auto native_result =
          z7::macos_integration::native_drag::run_macos_integration_native_drag(
              native_request);
      native_result_snapshot = native_result;
      if (native_result.handled) {
        if (callback) {
          DragExecutionReport report;
          report.archive_source = true;
          report.archive_transfer_requested = native_result.transfer_requested;
          report.internal_archive_drop_handled = false;
          report.native_archive_drag = true;
          report.native_source_widget_resolved =
              native_result.source_widget_resolved;
          report.native_view_available = native_result.native_view_available;
          report.native_current_event_available =
              native_result.current_event_available;
          report.native_drag_session_started =
              native_result.drag_session_started;
          report.native_drag_completed = native_result.drag_completed;
          report.native_ended_in_source_view =
              native_result.ended_in_source_view;
          report.native_ended_in_own_app_window =
              native_result.ended_in_own_app_window;
          report.native_promise_writes_settled =
              native_result.promise_writes_settled;
          report.native_timed_out = native_result.timed_out;
          report.native_drag_item_count = native_result.drag_item_count;
          report.promise_write_requests = native_result.promise_write_requests;
          report.promise_write_finishes = native_result.promise_write_finishes;
          report.promise_write_successes = native_result.promise_write_successes;
          report.direct_export_attempts =
              native_result.direct_export_attempts;
          report.direct_export_successes =
              native_result.direct_export_successes;
          report.native_error_message = native_result.error_message.trimmed();
          report.result_action = native_result.result_action;
          callback(report);
        }
        delete mime_data;
        return;
      }
      native_error_message = native_result.error_message.trimmed();
    }
  }

  if (callback) {
    DragExecutionReport report;
    report.archive_source = true;
    report.native_archive_drag = true;
    report.native_source_widget_resolved =
        native_result_snapshot.source_widget_resolved;
    report.native_view_available = native_result_snapshot.native_view_available;
    report.native_current_event_available =
        native_result_snapshot.current_event_available;
    report.native_drag_session_started =
        native_result_snapshot.drag_session_started;
    report.native_ended_in_source_view =
        native_result_snapshot.ended_in_source_view;
    report.native_ended_in_own_app_window =
        native_result_snapshot.ended_in_own_app_window;
    report.native_drag_item_count = native_result_snapshot.drag_item_count;
    report.native_promise_writes_settled =
        native_result_snapshot.promise_writes_settled;
    report.native_timed_out = native_result_snapshot.timed_out;
    report.promise_write_requests =
        native_result_snapshot.promise_write_requests;
    report.promise_write_finishes =
        native_result_snapshot.promise_write_finishes;
    report.promise_write_successes =
        native_result_snapshot.promise_write_successes;
    report.direct_export_attempts =
        native_result_snapshot.direct_export_attempts;
    report.direct_export_successes =
        native_result_snapshot.direct_export_successes;
    report.native_error_message = native_error_message;
    callback(report);
  }
  delete mime_data;
#else
  finish_standard_drag(view_ptr, supported_actions, mime_data, callback);
#endif
}

}  // namespace

DragAwareTreeView::DragAwareTreeView(QWidget* parent)
    : QTreeView(parent) {}

void DragAwareTreeView::set_drag_finished_callback(
    DragFinishedCallback callback) {
  drag_finished_callback_ = std::move(callback);
}

void DragAwareTreeView::set_archive_drag_materializer(
    ArchiveDragMaterializer materializer) {
  archive_drag_materializer_ = std::move(materializer);
}

void DragAwareTreeView::set_archive_drag_direct_exporter(
    ArchiveDragDirectExporter exporter) {
  archive_drag_direct_exporter_ = std::move(exporter);
}

void DragAwareTreeView::startDrag(Qt::DropActions supported_actions) {
  start_drag_with_callback(
      this,
      supported_actions,
      archive_drag_materializer_,
      archive_drag_direct_exporter_,
      drag_finished_callback_);
}

DragAwareListView::DragAwareListView(QWidget* parent)
    : QListView(parent) {}

void DragAwareListView::setModel(QAbstractItemModel* model) {
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
  reset_keyboard_shift_selection();
  QListView::setModel(model);
}

void DragAwareListView::set_alternative_selection_mode(bool enabled) {
  if (alternative_selection_mode_ == enabled) {
    return;
  }
  alternative_selection_mode_ = enabled;
  reset_keyboard_shift_selection();
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
}

void DragAwareListView::reset_keyboard_shift_selection() {
  keyboard_shift_anchor_ = QPersistentModelIndex();
  alternative_shift_selection_defined_ = false;
  alternative_shift_select_mark_ = true;
}

void DragAwareListView::set_drag_finished_callback(
    DragFinishedCallback callback) {
  drag_finished_callback_ = std::move(callback);
}

void DragAwareListView::set_archive_drag_materializer(
    ArchiveDragMaterializer materializer) {
  archive_drag_materializer_ = std::move(materializer);
}

void DragAwareListView::set_archive_drag_direct_exporter(
    ArchiveDragDirectExporter exporter) {
  archive_drag_direct_exporter_ = std::move(exporter);
}

bool DragAwareListView::handle_keyboard_shift_selection(QKeyEvent* event) {
  if (event == nullptr || event->key() == Qt::Key_Shift ||
      !event->modifiers().testFlag(Qt::ShiftModifier) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  QModelIndex target;
  switch (event->key()) {
    case Qt::Key_Up:
      target = moveCursor(MoveUp, event->modifiers());
      break;
    case Qt::Key_Down:
      target = moveCursor(MoveDown, event->modifiers());
      break;
    case Qt::Key_Left:
      target = moveCursor(MoveLeft, event->modifiers());
      break;
    case Qt::Key_Right:
      target = moveCursor(MoveRight, event->modifiers());
      break;
    default:
      return false;
  }
  if (!target.isValid() || target.row() == currentIndex().row()) {
    target = linear_shift_target_for_key(
        this, event->key(), DirectoryListModel::kNameColumn);
  }
  if (!apply_keyboard_shift_selection(this,
                                      target,
                                      DirectoryListModel::kNameColumn,
                                      alternative_selection_mode_,
                                      &keyboard_shift_anchor_,
                                      &alternative_shift_selection_defined_,
                                      &alternative_shift_select_mark_)) {
    return false;
  }
  event->accept();
  return true;
}

void DragAwareListView::keyPressEvent(QKeyEvent* event) {
  if (event != nullptr && event->key() == Qt::Key_Shift) {
    if (!event->isAutoRepeat()) {
      keyboard_shift_pressed_ = true;
      reset_keyboard_shift_selection();
    }
  } else if (event != nullptr &&
             !event->modifiers().testFlag(Qt::ShiftModifier)) {
    keyboard_shift_pressed_ = false;
    reset_keyboard_shift_selection();
  }

  if (handle_keyboard_shift_selection(event)) {
    return;
  }
  QListView::keyPressEvent(event);
}

void DragAwareListView::keyReleaseEvent(QKeyEvent* event) {
  if (event != nullptr && event->key() == Qt::Key_Shift &&
      !event->isAutoRepeat()) {
    keyboard_shift_pressed_ = false;
    reset_keyboard_shift_selection();
  }
  QListView::keyReleaseEvent(event);
}

void DragAwareListView::hideEvent(QHideEvent* event) {
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
  reset_keyboard_shift_selection();
  QListView::hideEvent(event);
}

void DragAwareListView::mousePressEvent(QMouseEvent* event) {
  if (event == nullptr || event->button() != Qt::LeftButton) {
    pending_custom_shift_click_release_ = false;
    reset_mouse_shift_anchor(&mouse_shift_anchor_,
                             &mouse_shift_anchor_model_,
                             &mouse_shift_anchor_row_);
    QListView::mousePressEvent(event);
#ifdef Z7_TESTING
    record_mouse_shift_press_diagnostic(
        this,
        -1,
        false,
        -1,
        -1,
        mouse_shift_modifier_state(this, event, keyboard_shift_pressed_),
        false,
        QStringLiteral("non_left_button"));
#endif
    return;
  }

  pending_custom_shift_click_release_ = false;
  setFocus(Qt::MouseFocusReason);
  const QModelIndex hit = indexAt(event->pos());
  const QModelIndex target = selection_column_index(
      this,
      hit,
      DirectoryListModel::kNameColumn);
  if (!target.isValid()) {
#ifdef Z7_TESTING
    const int anchor_row_before = mouse_shift_anchor_row_for_diagnostic(
        this,
        mouse_shift_anchor_,
        mouse_shift_anchor_model_,
        mouse_shift_anchor_row_);
#endif
    reset_mouse_shift_anchor(&mouse_shift_anchor_,
                             &mouse_shift_anchor_model_,
                             &mouse_shift_anchor_row_);
    pending_custom_shift_click_release_ = false;
    reset_keyboard_shift_selection();
    QListView::mousePressEvent(event);
#ifdef Z7_TESTING
    record_mouse_shift_press_diagnostic(
        this,
        hit.isValid() ? hit.row() : -1,
        false,
        anchor_row_before,
        -1,
        mouse_shift_modifier_state(this, event, keyboard_shift_pressed_),
        false,
        QStringLiteral("no_target"));
#endif
    return;
  }

  const MouseShiftModifierState shift_state =
      mouse_shift_modifier_state(this, event, keyboard_shift_pressed_);
#ifdef Z7_TESTING
  const int anchor_row_before = mouse_shift_anchor_row_for_diagnostic(
      this,
      mouse_shift_anchor_,
      mouse_shift_anchor_model_,
      mouse_shift_anchor_row_);
#endif
  if (shift_state.active()) {
    if (apply_mouse_shift_click_selection(this,
                                          target,
                                          DirectoryListModel::kNameColumn,
                                          &mouse_shift_anchor_,
                                          &mouse_shift_anchor_model_,
                                          &mouse_shift_anchor_row_,
                                          alternative_selection_mode_)) {
      reset_keyboard_shift_selection();
      pending_custom_shift_click_release_ = true;
      event->accept();
#ifdef Z7_TESTING
      record_mouse_shift_press_diagnostic(this,
                                          target.row(),
                                          true,
                                          anchor_row_before,
                                          mouse_shift_anchor_row_for_diagnostic(
                                              this,
                                              mouse_shift_anchor_,
                                              mouse_shift_anchor_model_,
                                              mouse_shift_anchor_row_),
                                          shift_state,
                                          true,
                                          QString());
#endif
      return;
    }
  }

  set_mouse_shift_anchor(target,
                         &mouse_shift_anchor_,
                         &mouse_shift_anchor_model_,
                         &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  reset_keyboard_shift_selection();
  QListView::mousePressEvent(event);
#ifdef Z7_TESTING
  record_mouse_shift_press_diagnostic(
      this,
      target.row(),
      true,
      anchor_row_before,
      mouse_shift_anchor_row_for_diagnostic(this,
                                            mouse_shift_anchor_,
                                            mouse_shift_anchor_model_,
                                            mouse_shift_anchor_row_),
      shift_state,
      false,
      shift_state.active() ? QStringLiteral("custom_failed")
                           : QStringLiteral("no_shift"));
#endif
}

void DragAwareListView::mouseReleaseEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton &&
      pending_custom_shift_click_release_) {
    pending_custom_shift_click_release_ = false;
    event->accept();
#ifdef Z7_TESTING
    record_mouse_shift_release_diagnostic(this, true);
#endif
    return;
  }
  QListView::mouseReleaseEvent(event);
#ifdef Z7_TESTING
  record_mouse_shift_release_diagnostic(this, false);
#endif
}

void DragAwareListView::startDrag(Qt::DropActions supported_actions) {
  start_drag_with_callback(
      this,
      supported_actions,
      archive_drag_materializer_,
      archive_drag_direct_exporter_,
      drag_finished_callback_);
}

DragAwareStructuredListView::DragAwareStructuredListView(QWidget* parent)
    : z7::ui::widgets::StructuredListView(parent) {}

void DragAwareStructuredListView::setModel(QAbstractItemModel* model) {
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
  reset_keyboard_shift_selection();
  z7::ui::widgets::StructuredListView::setModel(model);
}

void DragAwareStructuredListView::set_alternative_selection_mode(bool enabled) {
  if (alternative_selection_mode_ == enabled) {
    return;
  }
  alternative_selection_mode_ = enabled;
  reset_keyboard_shift_selection();
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
}

void DragAwareStructuredListView::reset_keyboard_shift_selection() {
  keyboard_shift_anchor_ = QPersistentModelIndex();
  alternative_shift_selection_defined_ = false;
  alternative_shift_select_mark_ = true;
}

void DragAwareStructuredListView::set_drag_finished_callback(
    DragFinishedCallback callback) {
  drag_finished_callback_ = std::move(callback);
}

void DragAwareStructuredListView::set_archive_drag_materializer(
    ArchiveDragMaterializer materializer) {
  archive_drag_materializer_ = std::move(materializer);
}

void DragAwareStructuredListView::set_archive_drag_direct_exporter(
    ArchiveDragDirectExporter exporter) {
  archive_drag_direct_exporter_ = std::move(exporter);
}

bool DragAwareStructuredListView::handle_keyboard_shift_selection(
    QKeyEvent* event) {
  if (event == nullptr || event->key() == Qt::Key_Shift ||
      !event->modifiers().testFlag(Qt::ShiftModifier) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  QModelIndex target;
  switch (event->key()) {
    case Qt::Key_Up:
      target = moveCursor(MoveUp, event->modifiers());
      break;
    case Qt::Key_Down:
      target = moveCursor(MoveDown, event->modifiers());
      break;
    case Qt::Key_Left:
      target = moveCursor(MoveLeft, event->modifiers());
      break;
    case Qt::Key_Right:
      target = moveCursor(MoveRight, event->modifiers());
      break;
    default:
      return false;
  }
  if (!target.isValid() || target.row() == currentIndex().row()) {
    target = linear_shift_target_for_key(this, event->key(), primary_column());
  }
  if (!apply_keyboard_shift_selection(this,
                                      target,
                                      primary_column(),
                                      alternative_selection_mode_,
                                      &keyboard_shift_anchor_,
                                      &alternative_shift_selection_defined_,
                                      &alternative_shift_select_mark_)) {
    return false;
  }
  event->accept();
  return true;
}

void DragAwareStructuredListView::keyPressEvent(QKeyEvent* event) {
  if (event != nullptr && event->key() == Qt::Key_Shift) {
    if (!event->isAutoRepeat()) {
      keyboard_shift_pressed_ = true;
      reset_keyboard_shift_selection();
    }
  } else if (event != nullptr &&
             !event->modifiers().testFlag(Qt::ShiftModifier)) {
    keyboard_shift_pressed_ = false;
    reset_keyboard_shift_selection();
  }

  if (handle_keyboard_shift_selection(event)) {
    return;
  }
  z7::ui::widgets::StructuredListView::keyPressEvent(event);
}

void DragAwareStructuredListView::keyReleaseEvent(QKeyEvent* event) {
  if (event != nullptr && event->key() == Qt::Key_Shift &&
      !event->isAutoRepeat()) {
    keyboard_shift_pressed_ = false;
    reset_keyboard_shift_selection();
  }
  z7::ui::widgets::StructuredListView::keyReleaseEvent(event);
}

void DragAwareStructuredListView::hideEvent(QHideEvent* event) {
  reset_mouse_shift_anchor(&mouse_shift_anchor_,
                           &mouse_shift_anchor_model_,
                           &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  keyboard_shift_pressed_ = false;
  reset_keyboard_shift_selection();
  z7::ui::widgets::StructuredListView::hideEvent(event);
}

void DragAwareStructuredListView::mousePressEvent(QMouseEvent* event) {
  if (event == nullptr || event->button() != Qt::LeftButton) {
    pending_custom_shift_click_release_ = false;
    reset_mouse_shift_anchor(&mouse_shift_anchor_,
                             &mouse_shift_anchor_model_,
                             &mouse_shift_anchor_row_);
    z7::ui::widgets::StructuredListView::mousePressEvent(event);
#ifdef Z7_TESTING
    record_mouse_shift_press_diagnostic(
        this,
        -1,
        false,
        -1,
        -1,
        mouse_shift_modifier_state(this, event, keyboard_shift_pressed_),
        false,
        QStringLiteral("non_left_button"));
#endif
    return;
  }

  pending_custom_shift_click_release_ = false;
  setFocus(Qt::MouseFocusReason);
  const QModelIndex hit = indexAt(event->pos());
  const bool primary_hit = is_primary_column(hit);
  const QModelIndex target = primary_hit ? normalize_to_primary_column(hit)
                                         : QModelIndex();
  if (!target.isValid()) {
#ifdef Z7_TESTING
    const int anchor_row_before = mouse_shift_anchor_row_for_diagnostic(
        this,
        mouse_shift_anchor_,
        mouse_shift_anchor_model_,
        mouse_shift_anchor_row_);
#endif
    reset_mouse_shift_anchor(&mouse_shift_anchor_,
                             &mouse_shift_anchor_model_,
                             &mouse_shift_anchor_row_);
    pending_custom_shift_click_release_ = false;
    reset_keyboard_shift_selection();
    z7::ui::widgets::StructuredListView::mousePressEvent(event);
#ifdef Z7_TESTING
    record_mouse_shift_press_diagnostic(
        this,
        hit.isValid() ? hit.row() : -1,
        primary_hit,
        anchor_row_before,
        -1,
        mouse_shift_modifier_state(this, event, keyboard_shift_pressed_),
        false,
        hit.isValid() ? QStringLiteral("non_primary_column")
                      : QStringLiteral("no_target"));
#endif
    return;
  }

  const MouseShiftModifierState shift_state =
      mouse_shift_modifier_state(this, event, keyboard_shift_pressed_);
#ifdef Z7_TESTING
  const int anchor_row_before = mouse_shift_anchor_row_for_diagnostic(
      this,
      mouse_shift_anchor_,
      mouse_shift_anchor_model_,
      mouse_shift_anchor_row_);
#endif
  if (shift_state.active()) {
    if (apply_mouse_shift_click_selection(this,
                                          target,
                                          primary_column(),
                                          &mouse_shift_anchor_,
                                          &mouse_shift_anchor_model_,
                                          &mouse_shift_anchor_row_,
                                          alternative_selection_mode_)) {
      reset_keyboard_shift_selection();
      pending_custom_shift_click_release_ = true;
      event->accept();
#ifdef Z7_TESTING
      record_mouse_shift_press_diagnostic(this,
                                          target.row(),
                                          true,
                                          anchor_row_before,
                                          mouse_shift_anchor_row_for_diagnostic(
                                              this,
                                              mouse_shift_anchor_,
                                              mouse_shift_anchor_model_,
                                              mouse_shift_anchor_row_),
                                          shift_state,
                                          true,
                                          QString());
#endif
      return;
    }
  }

  set_mouse_shift_anchor(target,
                         &mouse_shift_anchor_,
                         &mouse_shift_anchor_model_,
                         &mouse_shift_anchor_row_);
  pending_custom_shift_click_release_ = false;
  reset_keyboard_shift_selection();
  z7::ui::widgets::StructuredListView::mousePressEvent(event);
#ifdef Z7_TESTING
  record_mouse_shift_press_diagnostic(
      this,
      target.row(),
      true,
      anchor_row_before,
      mouse_shift_anchor_row_for_diagnostic(this,
                                            mouse_shift_anchor_,
                                            mouse_shift_anchor_model_,
                                            mouse_shift_anchor_row_),
      shift_state,
      false,
      shift_state.active() ? QStringLiteral("custom_failed")
                           : QStringLiteral("no_shift"));
#endif
}

void DragAwareStructuredListView::mouseReleaseEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton &&
      pending_custom_shift_click_release_) {
    pending_custom_shift_click_release_ = false;
    event->accept();
#ifdef Z7_TESTING
    record_mouse_shift_release_diagnostic(this, true);
#endif
    return;
  }
  z7::ui::widgets::StructuredListView::mouseReleaseEvent(event);
#ifdef Z7_TESTING
  record_mouse_shift_release_diagnostic(this, false);
#endif
}

void DragAwareStructuredListView::startDrag(Qt::DropActions supported_actions) {
  start_drag_with_callback(
      this,
      supported_actions,
      archive_drag_materializer_,
      archive_drag_direct_exporter_,
      drag_finished_callback_);
}

}  // namespace z7::ui::filemanager
