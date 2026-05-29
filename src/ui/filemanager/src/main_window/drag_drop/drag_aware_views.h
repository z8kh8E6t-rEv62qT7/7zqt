// src/ui/filemanager/src/main_window/drag_drop/drag_aware_views.h
// Role: Item-view wrappers that report drag execution results to MainWindow.

#pragma once

#include "drag_source_marker.h"

#include "structured_list_view.h"

#include <QListView>
#include <QStringList>
#include <QTreeView>

#include <functional>

namespace z7::ui::filemanager {

struct DragExecutionReport {
  bool archive_source = false;
  bool archive_transfer_requested = false;
  bool internal_archive_drop_handled = false;
  bool native_archive_drag = false;
  bool native_filesystem_drag = false;
  bool native_source_widget_resolved = false;
  bool native_view_available = false;
  bool native_current_event_available = false;
  bool native_drag_session_started = false;
  bool native_drag_completed = false;
  bool native_ended_in_source_view = false;
  bool native_ended_in_own_app_window = false;
  bool native_promise_writes_settled = false;
  bool native_timed_out = false;
  int native_drag_item_count = 0;
  int promise_write_requests = 0;
  int promise_write_finishes = 0;
  int promise_write_successes = 0;
  int direct_export_attempts = 0;
  int direct_export_successes = 0;
  QString native_error_message;
  QString materialization_error_message;
  Qt::DropAction result_action = Qt::IgnoreAction;
};

class DragAwareTreeView final : public QTreeView {
 public:
  using DragFinishedCallback = std::function<void(const DragExecutionReport&)>;
  using ArchiveDragMaterializedCallback =
      std::function<void(const QStringList&, const QString&)>;
  using ArchiveDragMaterializer =
      std::function<void(const QStringList&, ArchiveDragMaterializedCallback)>;
  using ArchiveDragDirectExporter =
      std::function<bool(const QString&, bool, const QString&, QString*)>;

  explicit DragAwareTreeView(QWidget* parent = nullptr);
  void set_drag_finished_callback(DragFinishedCallback callback);
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_archive_drag_direct_exporter(ArchiveDragDirectExporter exporter);

 protected:
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  DragFinishedCallback drag_finished_callback_;
  ArchiveDragMaterializer archive_drag_materializer_;
  ArchiveDragDirectExporter archive_drag_direct_exporter_;
};

class DragAwareListView final : public QListView {
 public:
  using DragFinishedCallback = std::function<void(const DragExecutionReport&)>;
  using ArchiveDragMaterializedCallback =
      std::function<void(const QStringList&, const QString&)>;
  using ArchiveDragMaterializer =
      std::function<void(const QStringList&, ArchiveDragMaterializedCallback)>;
  using ArchiveDragDirectExporter =
      std::function<bool(const QString&, bool, const QString&, QString*)>;

  explicit DragAwareListView(QWidget* parent = nullptr);
  void set_drag_finished_callback(DragFinishedCallback callback);
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_archive_drag_direct_exporter(ArchiveDragDirectExporter exporter);

 protected:
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  DragFinishedCallback drag_finished_callback_;
  ArchiveDragMaterializer archive_drag_materializer_;
  ArchiveDragDirectExporter archive_drag_direct_exporter_;
};

class DragAwareStructuredListView final : public z7::ui::widgets::StructuredListView {
  Q_OBJECT
 public:
  using DragFinishedCallback = std::function<void(const DragExecutionReport&)>;
  using ArchiveDragMaterializedCallback =
      std::function<void(const QStringList&, const QString&)>;
  using ArchiveDragMaterializer =
      std::function<void(const QStringList&, ArchiveDragMaterializedCallback)>;
  using ArchiveDragDirectExporter =
      std::function<bool(const QString&, bool, const QString&, QString*)>;

  explicit DragAwareStructuredListView(QWidget* parent = nullptr);
  void set_drag_finished_callback(DragFinishedCallback callback);
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_archive_drag_direct_exporter(ArchiveDragDirectExporter exporter);

 protected:
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  DragFinishedCallback drag_finished_callback_;
  ArchiveDragMaterializer archive_drag_materializer_;
  ArchiveDragDirectExporter archive_drag_direct_exporter_;
};

}  // namespace z7::ui::filemanager
