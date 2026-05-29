#include "internal.h"

#include <limits>

namespace z7::macos_integration::native_drag::detail {

SharedState::SharedState(const MacOSIntegrationNativeDragRequest& req,
                         const PromiseWriteConcurrencyPolicy& policy)
    : request(req),
      promise_write_requested_by_item(req.items.size(), false),
      promise_write_succeeded_by_item(req.items.size(), false),
      configured_concurrency_limit(
          qBound(kDefaultPromiseWriteConcurrency,
                 policy.configured_limit,
                 kMaxPromiseWriteConcurrency)),
      effective_concurrency_limit(configured_concurrency_limit),
      configured_large_file_threshold_bytes(
          qBound<qint64>(
              0, policy.large_file_threshold_bytes, kMaxLargeFileThresholdBytes)),
      configured_large_file_concurrency_limit(
          qBound(kDefaultLargeFileWriteConcurrency,
                 policy.large_file_concurrency_limit,
                 kMaxPromiseWriteConcurrency)),
      effective_large_file_concurrency_limit(
          configured_large_file_concurrency_limit) {
  effective_large_file_threshold_bytes = configured_large_file_threshold_bytes;
}

void SharedState::on_promise_request_started(int item_index) {
  {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    transfer_requested = true;
    ++promise_requests_started;
    if (item_index >= 0 && item_index < promise_write_requested_by_item.size()) {
      promise_write_requested_by_item[static_cast<qsizetype>(item_index)] = true;
    }
  }
  notify_completion_waiters();
}

void SharedState::set_source_view_screen_rect(const NSRect& rect, bool valid) {
  source_view_screen_rect = rect;
  source_view_screen_rect_valid = valid;
}

void SharedState::set_own_app_window_screen_rects(const QVector<NSRect>& rects) {
  own_app_window_screen_rects = rects;
}

void SharedState::mark_drag_session_ended(const Qt::DropAction action,
                                          bool ended_in_source_view_value,
                                          bool ended_in_own_app_window_value) {
  {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    drag_completed = true;
    ended_in_source_view = ended_in_source_view_value;
    ended_in_own_app_window = ended_in_own_app_window_value;
    result_action = action;
  }
  notify_completion_waiters();
}

void SharedState::acquire_write_slot() {
  std::unique_lock<std::mutex> lock(write_gate_mutex);
  write_gate_condition.wait(lock, [this]() {
    return active_write_slots < effective_concurrency_limit;
  });
  ++active_write_slots;
}

void SharedState::release_write_slot() {
  std::lock_guard<std::mutex> lock(write_gate_mutex);
  if (active_write_slots > 0) {
    --active_write_slots;
  }
  write_gate_condition.notify_one();
}

bool SharedState::should_use_large_file_throttle(qint64 payload_size_bytes) const {
  if (payload_size_bytes <= 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(write_gate_mutex);
  return effective_large_file_threshold_bytes > 0 &&
         payload_size_bytes >= effective_large_file_threshold_bytes;
}

void SharedState::acquire_large_file_write_slot() {
  std::unique_lock<std::mutex> lock(large_file_write_gate_mutex);
  large_file_write_gate_condition.wait(lock, [this]() {
    return active_large_file_write_slots < effective_large_file_concurrency_limit;
  });
  ++active_large_file_write_slots;
}

void SharedState::release_large_file_write_slot() {
  std::lock_guard<std::mutex> lock(large_file_write_gate_mutex);
  if (active_large_file_write_slots > 0) {
    --active_large_file_write_slots;
  }
  large_file_write_gate_condition.notify_one();
}

void SharedState::set_error_message_if_empty(const QString& value) {
  if (value.trimmed().isEmpty()) {
    return;
  }
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    if (error_message.trimmed().isEmpty()) {
      error_message = value.trimmed();
      updated = true;
    }
  }
  if (updated) {
    notify_completion_waiters();
  }
}

int SharedState::on_write_start() {
  std::lock_guard<std::mutex> lock(metrics_mutex);
  ++active_write_operations;
  if (active_write_operations > max_active_write_operations) {
    max_active_write_operations = active_write_operations;
  }
  return active_write_operations;
}

void SharedState::on_write_finish(qint64 direct_export_ms,
                                  qint64 total_ms,
                                  bool success,
                                  bool direct_export_attempted,
                                  bool direct_export_succeeded,
                                  qint64 payload_size_bytes,
                                  bool large_file_mode,
                                  int item_index) {
  std::lock_guard<std::mutex> lock(metrics_mutex);
  ++write_attempts;
  if (success) {
    ++write_successes;
  } else {
    ++write_failures;
  }
  if (item_index >= 0 && item_index < promise_write_succeeded_by_item.size()) {
    promise_write_succeeded_by_item[static_cast<qsizetype>(item_index)] = success;
  }
  if (direct_export_attempted) {
    ++direct_export_attempts;
    if (direct_export_succeeded) {
      ++direct_export_successes;
    }
  }
  ++promise_requests_finished;
  const qint64 normalized_payload_bytes = qMax<qint64>(0, payload_size_bytes);
  if (normalized_payload_bytes > max_single_write_bytes) {
    max_single_write_bytes = normalized_payload_bytes;
  }
  if (large_file_mode) {
    ++large_file_write_attempts;
    if (success) {
      ++large_file_write_successes;
    } else {
      ++large_file_write_failures;
    }
    if (std::numeric_limits<qint64>::max() - large_file_total_bytes <
        normalized_payload_bytes) {
      large_file_total_bytes = std::numeric_limits<qint64>::max();
    } else {
      large_file_total_bytes += normalized_payload_bytes;
    }
  }
  total_direct_export_ms += qMax<qint64>(0, direct_export_ms);
  total_total_ms += qMax<qint64>(0, total_ms);
  if (active_write_operations > 0) {
    --active_write_operations;
  }
  notify_completion_waiters();
}

void SharedState::notify_completion_waiters() {
  {
    std::lock_guard<std::mutex> lock(completion_wait_mutex);
    ++completion_wait_epoch;
  }
  completion_wait_condition.notify_all();
}

bool SharedState::should_finish_waiting() const {
  std::lock_guard<std::mutex> lock(metrics_mutex);
  if (!drag_completed) {
    return false;
  }

  if (promise_requests_started == 0) {
    return true;
  }
  return promise_requests_finished >= promise_requests_started;
}

SharedState::MetricsSnapshot SharedState::metrics_snapshot() const {
  std::lock_guard<std::mutex> lock(metrics_mutex);
  MetricsSnapshot snapshot;
  snapshot.transfer_requested = transfer_requested;
  snapshot.drag_session_ended = drag_completed;
  snapshot.ended_in_source_view = ended_in_source_view;
  snapshot.ended_in_own_app_window = ended_in_own_app_window;
  snapshot.result_action = result_action;
  snapshot.error_message = error_message;
  snapshot.promise_requests_started = promise_requests_started;
  snapshot.promise_requests_finished = promise_requests_finished;
  snapshot.promise_write_requested_by_item = promise_write_requested_by_item;
  snapshot.promise_write_succeeded_by_item = promise_write_succeeded_by_item;
  snapshot.write_attempts = write_attempts;
  snapshot.write_successes = write_successes;
  snapshot.write_failures = write_failures;
  snapshot.direct_export_attempts = direct_export_attempts;
  snapshot.direct_export_successes = direct_export_successes;
  snapshot.max_active_write_operations = max_active_write_operations;
  snapshot.total_direct_export_ms = total_direct_export_ms;
  snapshot.total_total_ms = total_total_ms;
  snapshot.configured_concurrency_limit = configured_concurrency_limit;
  snapshot.configured_large_file_threshold_bytes =
      configured_large_file_threshold_bytes;
  snapshot.configured_large_file_concurrency_limit =
      configured_large_file_concurrency_limit;
  snapshot.large_file_write_attempts = large_file_write_attempts;
  snapshot.large_file_write_successes = large_file_write_successes;
  snapshot.large_file_write_failures = large_file_write_failures;
  snapshot.large_file_total_bytes = large_file_total_bytes;
  snapshot.max_single_write_bytes = max_single_write_bytes;
  {
    std::lock_guard<std::mutex> write_lock(write_gate_mutex);
    snapshot.effective_concurrency_limit = effective_concurrency_limit;
    snapshot.effective_large_file_threshold_bytes =
        effective_large_file_threshold_bytes;
  }
  {
    std::lock_guard<std::mutex> large_file_lock(large_file_write_gate_mutex);
    snapshot.effective_large_file_concurrency_limit =
        effective_large_file_concurrency_limit;
  }
  return snapshot;
}

ScopedWriteSlotAcquire::ScopedWriteSlotAcquire(SharedState* shared)
    : shared_(shared) {
  if (shared_ != nullptr) {
    shared_->acquire_write_slot();
  }
}

ScopedWriteSlotAcquire::~ScopedWriteSlotAcquire() {
  if (shared_ != nullptr) {
    shared_->release_write_slot();
  }
}

ScopedLargeFileWriteSlotAcquire::ScopedLargeFileWriteSlotAcquire(
    SharedState* shared,
    bool enable)
    : shared_(enable ? shared : nullptr) {
  if (shared_ != nullptr) {
    shared_->acquire_large_file_write_slot();
  }
}

ScopedLargeFileWriteSlotAcquire::~ScopedLargeFileWriteSlotAcquire() {
  if (shared_ != nullptr) {
    shared_->release_large_file_write_slot();
  }
}

}  // namespace z7::macos_integration::native_drag::detail
