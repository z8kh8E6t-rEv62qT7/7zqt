#include "task_ipc_runtime_internal.h"

#include <QSharedMemory>

#include <atomic>

namespace z7::task_ipc_runtime {
namespace {

TaskIpcPayload decode_posix_payload(const task_ipc_internal::TaskIpcPerTaskRaw& raw) {
  using namespace task_ipc_internal;

  TaskIpcPayload payload;
  QString payload_error;
  const QByteArray payload_bytes(raw.payload,
                                 static_cast<int>(raw.slot.request_payload_size));
  if (!deserialize_task_payload(payload_bytes, &payload, &payload_error)) {
    payload = TaskIpcPayload{};
  }
  return payload;
}

#if !defined(Q_OS_MACOS)
TaskIpcPayload decode_non_posix_payload(
    QSharedMemory* request_pool_memory,
    task_ipc_internal::TaskIpcRequestPoolHeaderRaw* request_pool,
    const task_ipc_internal::TaskIpcSlotRaw& slot) {
  using namespace task_ipc_internal;

  TaskIpcPayload payload;
  if (request_pool == nullptr || request_pool_memory == nullptr ||
      slot.request_payload_size == 0U) {
    return payload;
  }

  QString payload_error;
  SharedMemoryLock slot_lock(
      request_pool_slot_lock(request_pool, static_cast<int>(slot.request_pool_slot)));
  if (slot_lock.ok()) {
    if (!read_request_payload_from_slot(request_pool_memory,
                                        static_cast<int>(slot.request_pool_slot),
                                        slot.request_payload_size,
                                        &payload,
                                        &payload_error)) {
      payload = TaskIpcPayload{};
    }
  }
  return payload;
}
#endif

TaskIpcEvent build_task_ipc_event(const task_ipc_internal::TaskIpcSlotRaw& slot,
                                  const QString& owner_instance_id,
                                  quint32 event_sequence,
                                  const TaskIpcPayload& payload) {
  TaskIpcEvent event;
  event.session_id = slot.session_id;
  event.generation = slot.generation;
  event.event_kind =
      task_ipc_internal::task_ipc_event_kind_for_sequence(event_sequence);
  event.event_sequence = event_sequence;
  event.owner_instance_id = owner_instance_id;
  event.worker_pid = slot.worker_pid;
  event.result_code = slot.result_code;
  event.refresh_after_finish = slot.refresh_after_finish != 0;
  event.summary = task_ipc_internal::read_fixed_utf8(slot.summary, 256);
  event.payload = payload;
  return event;
}

void append_pending_events_for_slot(const task_ipc_internal::TaskIpcSlotRaw& slot,
                                    const QString& owner_instance_id,
                                    const TaskIpcPayload& payload,
                                    QVector<TaskIpcEvent>* out_events) {
  using namespace task_ipc_internal;

  if (out_events == nullptr || !slot_has_pending_task_ipc_events(slot)) {
    return;
  }
  for (quint32 sequence = next_pending_task_ipc_event_sequence(slot);
       sequence != 0U && sequence <= slot.published_event_sequence; ++sequence) {
    out_events->push_back(
        build_task_ipc_event(slot, owner_instance_id, sequence, payload));
  }
}

}  // namespace

bool publish_task_ipc_completion(const TaskIpcClaimedTask& task,
                                 int result_code, const QString& summary,
                                 QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (task.slot_index < 0 || task.slot_index >= kTaskIpcSlotCount) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC claimed task slot index is invalid.");
    }
    return false;
  }

  if (task.launcher_pid > 0 && !process_is_alive(task.launcher_pid)) {
    return true;
  }

#if defined(Q_OS_MACOS)
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      PosixTaskIpcMapping::open_worker(task.ipc_shm_name, task.ipc_sem_name,
                                       error_message);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    if (task.launcher_pid > 0 && !process_is_alive(task.launcher_pid)) {
      return true;
    }
    return false;
  }

  {
    TaskIpcPerTaskRaw* raw = mapping->raw();
    std::unique_ptr<SharedMemoryLock> lock = wait_for_shared_memory_lock(
        &raw->lock, task_ipc_per_task_lock_wake_key(mapping->shm_name()),
        kCompletionPublishWaitMsecs,
        QStringLiteral("Task IPC task remained busy while publishing completion."),
        error_message);
    if (lock == nullptr) {
      if (error_message != nullptr) {
        *error_message =
            error_message->trimmed().isEmpty()
                ? QStringLiteral("Task IPC task is busy.")
                : error_message->trimmed();
      }
      return false;
    }

    TaskIpcSlotRaw& slot = raw->slot;
    if (!slot_matches_claim(slot, task)) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Task IPC task slot no longer matches claimed task.");
      }
      return false;
    }
    static_cast<void>(
        publish_task_ipc_completion_event(&slot, result_code, summary, now_msecs()));
  }
  return post_posix_task_notification(mapping.get(), error_message);
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(false, &bootstrap_memory, error_message)) {
    if (task.launcher_pid > 0 && !process_is_alive(task.launcher_pid)) {
      return true;
    }
    return false;
  }

  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  if (bootstrap == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC bootstrap pointer is null.");
    }
    return false;
  }
  const QString bootstrap_lock_wake_key = task_ipc_bootstrap_lock_wake_key();

  std::unique_ptr<SharedMemoryLock> bootstrap_lock = wait_for_shared_memory_lock(
      &bootstrap->lock, bootstrap_lock_wake_key, kCompletionPublishWaitMsecs,
      QStringLiteral(
          "Task IPC bootstrap remained busy while publishing completion."),
      error_message);
  if (bootstrap_lock == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          error_message->trimmed().isEmpty()
              ? QStringLiteral("Task IPC bootstrap is busy.")
              : error_message->trimmed();
    }
    return false;
  }

  TaskIpcSlotRaw& slot = bootstrap->slot_records[task.slot_index];
  if (!slot_matches_claim(slot, task)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task slot no longer matches claimed task.");
    }
    return false;
  }
  static_cast<void>(
      publish_task_ipc_completion_event(&slot, result_code, summary, now_msecs()));
  bootstrap_lock.reset();
  return post_task_ipc_semaphore(
      non_posix_task_ipc_event_semaphore_key(task.owner_instance_id),
      error_message);
#endif
}

bool publish_task_ipc_completion_minimal(const TaskIpcClaimedTask& task,
                                         int result_code,
                                         QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (task.slot_index < 0 || task.slot_index >= kTaskIpcSlotCount) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC claimed task slot index is invalid.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      PosixTaskIpcMapping::open_worker(task.ipc_shm_name, task.ipc_sem_name,
                                       error_message);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return false;
  }

  TaskIpcSlotRaw& slot = mapping->raw()->slot;
  if (!slot_matches_claim(slot, task)) {
    return true;
  }

  std::atomic_ref<quint32> state(slot.state);
  std::atomic_ref<quint32> published_event_sequence(slot.published_event_sequence);
  std::atomic_ref<qint32> result(slot.result_code);
  std::atomic_ref<qint64> updated(slot.updated_msecs);

  result.store(result_code, std::memory_order_release);
  updated.store(now_msecs(), std::memory_order_release);
  published_event_sequence.store(kTaskIpcCompletedEventSequence,
                                 std::memory_order_release);
  state.store(static_cast<quint32>(TaskIpcSlotState::kCompleted),
              std::memory_order_release);
  return post_posix_task_notification(mapping.get(), error_message);
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(false, &bootstrap_memory, error_message)) {
    return false;
  }
  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  if (bootstrap == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC bootstrap pointer is null.");
    }
    return false;
  }

  TaskIpcSlotRaw& slot = bootstrap->slot_records[task.slot_index];
  if (!slot_matches_claim(slot, task)) {
    return true;
  }

  std::atomic_ref<quint32> state(slot.state);
  std::atomic_ref<quint32> published_event_sequence(slot.published_event_sequence);
  std::atomic_ref<qint32> result(slot.result_code);
  std::atomic_ref<qint64> updated(slot.updated_msecs);

  result.store(result_code, std::memory_order_release);
  updated.store(now_msecs(), std::memory_order_release);
  published_event_sequence.store(kTaskIpcCompletedEventSequence,
                                 std::memory_order_release);
  state.store(static_cast<quint32>(TaskIpcSlotState::kCompleted),
              std::memory_order_release);
  return post_task_ipc_semaphore(
      non_posix_task_ipc_event_semaphore_key(task.owner_instance_id),
      error_message);
#endif
}

bool collect_task_ipc_events(const QString& owner_instance_id,
                             QVector<TaskIpcEvent>* out_events,
                             QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out_events == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing task IPC event output.");
    }
    return false;
  }
  out_events->clear();

  QString normalized_owner_instance_id;
  if (!validate_task_ipc_owner_instance_id(owner_instance_id,
                                           &normalized_owner_instance_id,
                                           error_message)) {
    return false;
  }

#if defined(Q_OS_MACOS)
  const QVector<std::shared_ptr<PosixTaskIpcMapping>> mappings =
      posix_task_mappings_snapshot();
  for (const std::shared_ptr<PosixTaskIpcMapping>& mapping : mappings) {
    if (mapping == nullptr || mapping->raw() == nullptr) {
      continue;
    }
    TaskIpcPerTaskRaw* raw = mapping->raw();
    SharedMemoryLock lock(&raw->lock,
                          task_ipc_per_task_lock_wake_key(mapping->shm_name()));
    if (!lock.ok()) {
      if (lock.busy()) {
        continue;
      }
      if (error_message != nullptr) {
        *error_message = lock.error();
      }
      return false;
    }

    const TaskIpcSlotRaw& slot = raw->slot;
    if (slot.state == static_cast<quint32>(TaskIpcSlotState::kEmpty)) {
      remove_posix_task_mapping(mapping);
      continue;
    }
    if (!slot_matches_owner(slot, normalized_owner_instance_id)) {
      continue;
    }
    append_pending_events_for_slot(slot, normalized_owner_instance_id,
                                   decode_posix_payload(*raw), out_events);
  }
  return true;
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(false, &bootstrap_memory, error_message)) {
    return false;
  }
  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  if (bootstrap == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC bootstrap pointer is null.");
    }
    return false;
  }
  const QString bootstrap_lock_wake_key = task_ipc_bootstrap_lock_wake_key();

  std::unique_ptr<SharedMemoryLock> bootstrap_lock = wait_for_shared_memory_lock(
      &bootstrap->lock, bootstrap_lock_wake_key, kCompletionPublishWaitMsecs,
      QStringLiteral("Task IPC bootstrap remained busy while collecting events."),
      error_message);
  if (bootstrap_lock == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          error_message->trimmed().isEmpty()
              ? QStringLiteral("Task IPC bootstrap is busy.")
              : error_message->trimmed();
    }
    return false;
  }

  std::shared_ptr<QSharedMemory> request_pool_memory;
  TaskIpcRequestPoolHeaderRaw* request_pool = nullptr;
  {
    QString request_pool_error;
    if (open_request_pool_memory(false, &request_pool_memory, &request_pool_error)) {
      request_pool = request_pool_raw(request_pool_memory.get());
    }
  }

  reclaim_stale_slots(bootstrap, now_msecs());

  for (int i = 0; i < kTaskIpcSlotCount; ++i) {
    const TaskIpcSlotRaw& slot = bootstrap->slot_records[i];
    if (slot.state == static_cast<quint32>(TaskIpcSlotState::kEmpty)) {
      continue;
    }
    if (!slot_matches_owner(slot, normalized_owner_instance_id)) {
      continue;
    }
    append_pending_events_for_slot(
        slot, normalized_owner_instance_id,
        decode_non_posix_payload(request_pool_memory.get(), request_pool, slot),
        out_events);
  }

  return true;
#endif
}

bool acknowledge_task_ipc_event(const TaskIpcEvent& event,
                                QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  QString normalized_owner_instance_id;
  if (!validate_task_ipc_owner_instance_id(event.owner_instance_id,
                                           &normalized_owner_instance_id,
                                           error_message)) {
    return false;
  }
  const quint32 event_sequence = task_ipc_event_sequence_for_kind(event.event_kind);
  if (event_sequence == 0U || event_sequence != event.event_sequence) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC event identity is invalid.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      find_posix_task_mapping(event.session_id, event.generation);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return true;
  }

  bool should_remove_mapping = false;
  TaskIpcPerTaskRaw* raw = mapping->raw();
  {
    SharedMemoryLock lock(&raw->lock,
                          task_ipc_per_task_lock_wake_key(mapping->shm_name()));
    if (!lock.ok()) {
      if (error_message != nullptr) {
        *error_message = lock.busy() ? QStringLiteral("Task IPC task is busy.")
                                     : lock.error();
      }
      return false;
    }

    TaskIpcSlotRaw& slot = raw->slot;
    if (!slot_matches_event(slot, event)) {
      return true;
    }
    if (slot.acknowledged_event_sequence < event_sequence) {
      slot.acknowledged_event_sequence = event_sequence;
      slot.updated_msecs = now_msecs();
    }
    should_remove_mapping =
        slot.acknowledged_event_sequence >= kTaskIpcCompletedEventSequence;
  }

  if (should_remove_mapping) {
    remove_posix_task_mapping(mapping);
  }
  return true;
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(false, &bootstrap_memory, error_message)) {
    return false;
  }
  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  if (bootstrap == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC bootstrap pointer is null.");
    }
    return false;
  }

  SharedMemoryLock bootstrap_lock(&bootstrap->lock,
                                  task_ipc_bootstrap_lock_wake_key());
  if (!bootstrap_lock.ok()) {
    if (error_message != nullptr) {
      *error_message = bootstrap_lock.busy()
                           ? QStringLiteral("Task IPC bootstrap is busy.")
                           : bootstrap_lock.error();
    }
    return false;
  }

  for (int i = 0; i < kTaskIpcSlotCount; ++i) {
    TaskIpcSlotRaw& slot = bootstrap->slot_records[i];
    if (slot.session_id != event.session_id ||
        slot.generation != event.generation) {
      continue;
    }
    if (!slot_matches_event(slot, event)) {
      return true;
    }
    if (slot.acknowledged_event_sequence < event_sequence) {
      slot.acknowledged_event_sequence = event_sequence;
      slot.updated_msecs = now_msecs();
    }
    if (slot.acknowledged_event_sequence >= kTaskIpcCompletedEventSequence) {
      clear_slot(&slot, true);
      slot.updated_msecs = now_msecs();
    }
    return true;
  }
  return true;
#endif
}

}  // namespace z7::task_ipc_runtime
