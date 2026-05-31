// src/ui/filemanager/src/main_window/drag_drop/drag_aware_views.h
// Role: Item-view wrappers that report drag execution results to MainWindow.

#pragma once

#include "drag_source_marker.h"

#include "structured_list_view.h"

#include <QListView>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStringList>
#include <QTreeView>

#include <functional>

class QAbstractItemModel;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;

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
  void set_alternative_selection_mode(bool enabled);
  void setModel(QAbstractItemModel* model) override;
  void reset_keyboard_shift_selection();
  void set_drag_finished_callback(DragFinishedCallback callback);
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_archive_drag_direct_exporter(ArchiveDragDirectExporter exporter);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  bool handle_keyboard_shift_selection(QKeyEvent* event);

  DragFinishedCallback drag_finished_callback_;
  ArchiveDragMaterializer archive_drag_materializer_;
  ArchiveDragDirectExporter archive_drag_direct_exporter_;
  QPersistentModelIndex keyboard_shift_anchor_;
  QPersistentModelIndex mouse_shift_anchor_;
  QPointer<QAbstractItemModel> mouse_shift_anchor_model_;
  int mouse_shift_anchor_row_ = -1;
  bool pending_custom_shift_click_release_ = false;
  bool keyboard_shift_pressed_ = false;
  bool alternative_selection_mode_ = false;
  bool alternative_shift_selection_defined_ = false;
  bool alternative_shift_select_mark_ = true;
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
  void set_alternative_selection_mode(bool enabled);
  void setModel(QAbstractItemModel* model) override;
  void reset_keyboard_shift_selection();
  void set_drag_finished_callback(DragFinishedCallback callback);
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_archive_drag_direct_exporter(ArchiveDragDirectExporter exporter);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  bool handle_keyboard_shift_selection(QKeyEvent* event);

  DragFinishedCallback drag_finished_callback_;
  ArchiveDragMaterializer archive_drag_materializer_;
  ArchiveDragDirectExporter archive_drag_direct_exporter_;
  QPersistentModelIndex keyboard_shift_anchor_;
  QPersistentModelIndex mouse_shift_anchor_;
  QPointer<QAbstractItemModel> mouse_shift_anchor_model_;
  int mouse_shift_anchor_row_ = -1;
  bool pending_custom_shift_click_release_ = false;
  bool keyboard_shift_pressed_ = false;
  bool alternative_selection_mode_ = false;
  bool alternative_shift_selection_defined_ = false;
  bool alternative_shift_select_mark_ = true;
};

}  // namespace z7::ui::filemanager
