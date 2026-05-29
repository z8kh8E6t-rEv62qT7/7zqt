#pragma once

#include <QVector>
#include <QString>
#include <Qt>

#include <functional>

class QWidget;

namespace z7::macos_integration::native_drag {

enum class MacOSIntegrationNativeDragKind {
  kArchive = 0,
  kFilesystem = 1
};

struct MacOSIntegrationNativeDragItem {
  QString archive_entry_path;
  QString source_path;
  std::function<bool(const QString&, QString*)>
      write_to_destination;
  std::function<qint64()> estimate_payload_size_bytes;
  QString suggested_file_name;
  QString file_type_identifier;
  bool is_dir = false;
};

struct MacOSIntegrationNativeDragRequest {
  QWidget* source_widget = nullptr;
  MacOSIntegrationNativeDragKind kind =
      MacOSIntegrationNativeDragKind::kArchive;
  Qt::DropActions supported_actions = Qt::CopyAction;
  QVector<MacOSIntegrationNativeDragItem> items;
};

struct MacOSIntegrationNativeDragResult {
  bool handled = false;
  bool source_widget_resolved = false;
  bool native_view_available = false;
  bool current_event_available = false;
  bool drag_session_started = false;
  bool drag_completed = false;
  bool ended_in_source_view = false;
  bool ended_in_own_app_window = false;
  bool promise_writes_settled = false;
  bool timed_out = false;
  bool transfer_requested = false;
  int drag_item_count = 0;
  int promise_write_requests = 0;
  int promise_write_finishes = 0;
  int promise_write_successes = 0;
  int direct_export_attempts = 0;
  int direct_export_successes = 0;
  Qt::DropAction result_action = Qt::IgnoreAction;
  QString error_message;
};

MacOSIntegrationNativeDragResult run_macos_integration_native_drag(
    const MacOSIntegrationNativeDragRequest& request);

}  // namespace z7::macos_integration::native_drag
