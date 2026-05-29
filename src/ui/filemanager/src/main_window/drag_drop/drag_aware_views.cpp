// src/ui/filemanager/src/main_window/drag_drop/drag_aware_views.cpp
// Role: Start-drag wrappers that emit archive drag completion diagnostics.

#include "drag_aware_views.h"

#include "main_window/model/model.h"

#include <QAbstractItemModel>
#include <QDrag>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMimeData>
#include <QPointer>
#include <QSet>

#include <memory>
#include <limits>

#include "drag_drop_policy_qt.h"
#include "structured_list_proxy.h"

#if defined(Q_OS_MAC)
#include "macos_native_drag.h"
#endif

namespace z7::ui::filemanager {

namespace {

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

void DragAwareStructuredListView::startDrag(Qt::DropActions supported_actions) {
  start_drag_with_callback(
      this,
      supported_actions,
      archive_drag_materializer_,
      archive_drag_direct_exporter_,
      drag_finished_callback_);
}

}  // namespace z7::ui::filemanager
