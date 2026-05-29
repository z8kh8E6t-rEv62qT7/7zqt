#pragma once

#include "macos_native_drag.h"

#include <QtGlobal>

#include <QMetaObject>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#if defined(Q_OS_MAC)
#import <AppKit/AppKit.h>
#endif

namespace z7::macos_integration::native_drag::detail {

constexpr int kDefaultPromiseWriteConcurrency = 1;
constexpr int kMaxPromiseWriteConcurrency = 32;
constexpr qint64 kBytesPerMiB = 1024LL * 1024LL;
constexpr qint64 kDefaultLargeFileThresholdBytes = 512LL * kBytesPerMiB;
constexpr qint64 kMaxLargeFileThresholdBytes = 1024LL * 1024LL * 1024LL * 1024LL;
constexpr int kDefaultLargeFileWriteConcurrency = 1;

Qt::DropAction qt_action_from_ns_operation(NSDragOperation op);
NSString* to_ns_string(const QString& value);
QString to_q_string(NSString* value);
QString to_q_string(NSURL* url);
NSError* make_copy_error(const QString& message);

struct PromiseWriteConcurrencyPolicy {
  int configured_limit = kDefaultPromiseWriteConcurrency;
  qint64 large_file_threshold_bytes = kDefaultLargeFileThresholdBytes;
  int large_file_concurrency_limit = kDefaultLargeFileWriteConcurrency;

  // Returns a policy built entirely from compiled-in defaults.
  // All env vars, QWidget properties, and runtime whitelist adjustments have
  // been removed (§3.1); limits are the module constants.
  static PromiseWriteConcurrencyPolicy from_defaults();
};

QString default_item_name(const MacOSIntegrationNativeDragItem& item);
QString native_drag_log_prefix(MacOSIntegrationNativeDragKind kind);
bool copy_source_path_to_destination(const QString& source_path,
                                     bool source_is_dir,
                                     const QString& target_directory,
                                     const QString& promised_name,
                                     QString* error);
bool write_to_destination_if_needed(MacOSIntegrationNativeDragItem* item,
                                    const QString& destination_path,
                                    QString* error);

struct SharedState {
  struct MetricsSnapshot {
    bool transfer_requested = false;
    bool drag_session_ended = false;
    bool ended_in_source_view = false;
    bool ended_in_own_app_window = false;
    Qt::DropAction result_action = Qt::IgnoreAction;
    QString error_message;
    int promise_requests_started = 0;
    int promise_requests_finished = 0;
    QVector<bool> promise_write_requested_by_item;
    QVector<bool> promise_write_succeeded_by_item;
    int write_attempts = 0;
    int write_successes = 0;
    int write_failures = 0;
    int direct_export_attempts = 0;
    int direct_export_successes = 0;
    int large_file_write_attempts = 0;
    int large_file_write_successes = 0;
    int large_file_write_failures = 0;
    int max_active_write_operations = 0;
    int configured_concurrency_limit = kDefaultPromiseWriteConcurrency;
    int effective_concurrency_limit = kDefaultPromiseWriteConcurrency;
    qint64 configured_large_file_threshold_bytes =
        kDefaultLargeFileThresholdBytes;
    qint64 effective_large_file_threshold_bytes =
        kDefaultLargeFileThresholdBytes;
    int configured_large_file_concurrency_limit =
        kDefaultLargeFileWriteConcurrency;
    int effective_large_file_concurrency_limit =
        kDefaultLargeFileWriteConcurrency;
    qint64 large_file_total_bytes = 0;
    qint64 max_single_write_bytes = 0;
    qint64 total_direct_export_ms = 0;
    qint64 total_total_ms = 0;
  };

  explicit SharedState(const MacOSIntegrationNativeDragRequest& req,
                       const PromiseWriteConcurrencyPolicy& policy);

  void on_promise_request_started(int item_index);
  void set_source_view_screen_rect(const NSRect& rect, bool valid);
  void set_own_app_window_screen_rects(const QVector<NSRect>& rects);
  void mark_drag_session_ended(Qt::DropAction result_action,
                               bool ended_in_source_view,
                               bool ended_in_own_app_window);
  void acquire_write_slot();
  void release_write_slot();
  bool should_use_large_file_throttle(qint64 payload_size_bytes) const;
  void acquire_large_file_write_slot();
  void release_large_file_write_slot();
  void set_error_message_if_empty(const QString& value);
  int on_write_start();
  void on_write_finish(qint64 direct_export_ms,
                       qint64 total_ms,
                       bool success,
                       bool direct_export_attempted,
                       bool direct_export_succeeded,
                       qint64 payload_size_bytes,
                       bool large_file_mode,
                       int item_index);
  void notify_completion_waiters();
  bool should_finish_waiting() const;
  MetricsSnapshot metrics_snapshot() const;

  MacOSIntegrationNativeDragRequest request;
  NSRect source_view_screen_rect = NSZeroRect;
  bool source_view_screen_rect_valid = false;
  QVector<NSRect> own_app_window_screen_rects;
  bool transfer_requested = false;
  bool drag_completed = false;
  bool ended_in_source_view = false;
  bool ended_in_own_app_window = false;
  int promise_requests_started = 0;
  int promise_requests_finished = 0;
  QVector<bool> promise_write_requested_by_item;
  QVector<bool> promise_write_succeeded_by_item;
  Qt::DropAction result_action = Qt::IgnoreAction;
  QString error_message;
  const int configured_concurrency_limit = kDefaultPromiseWriteConcurrency;
  int effective_concurrency_limit = kDefaultPromiseWriteConcurrency;
  const qint64 configured_large_file_threshold_bytes =
      kDefaultLargeFileThresholdBytes;
  qint64 effective_large_file_threshold_bytes =
      kDefaultLargeFileThresholdBytes;
  const int configured_large_file_concurrency_limit =
      kDefaultLargeFileWriteConcurrency;
  int effective_large_file_concurrency_limit = kDefaultLargeFileWriteConcurrency;
  int active_write_slots = 0;
  mutable std::mutex write_gate_mutex;
  std::condition_variable write_gate_condition;
  int active_large_file_write_slots = 0;
  mutable std::mutex large_file_write_gate_mutex;
  std::condition_variable large_file_write_gate_condition;
  mutable std::mutex metrics_mutex;
  int write_attempts = 0;
  int write_successes = 0;
  int write_failures = 0;
  int direct_export_attempts = 0;
  int direct_export_successes = 0;
  int large_file_write_attempts = 0;
  int large_file_write_successes = 0;
  int large_file_write_failures = 0;
  qint64 large_file_total_bytes = 0;
  qint64 max_single_write_bytes = 0;
  int active_write_operations = 0;
  int max_active_write_operations = 0;
  qint64 total_direct_export_ms = 0;
  qint64 total_total_ms = 0;
  mutable std::mutex completion_wait_mutex;
  std::condition_variable completion_wait_condition;
  quint64 completion_wait_epoch = 0;
};

class ScopedWriteSlotAcquire final {
 public:
  explicit ScopedWriteSlotAcquire(SharedState* shared);
  ~ScopedWriteSlotAcquire();
  ScopedWriteSlotAcquire(const ScopedWriteSlotAcquire&) = delete;
  ScopedWriteSlotAcquire& operator=(const ScopedWriteSlotAcquire&) = delete;

 private:
  SharedState* shared_ = nullptr;
};

class ScopedLargeFileWriteSlotAcquire final {
 public:
  ScopedLargeFileWriteSlotAcquire(SharedState* shared, bool enable);
  ~ScopedLargeFileWriteSlotAcquire();
  ScopedLargeFileWriteSlotAcquire(const ScopedLargeFileWriteSlotAcquire&) = delete;
  ScopedLargeFileWriteSlotAcquire& operator=(
      const ScopedLargeFileWriteSlotAcquire&) = delete;

 private:
  SharedState* shared_ = nullptr;
};

}  // namespace z7::macos_integration::native_drag::detail
