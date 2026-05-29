#include "task_ipc_runtime_internal.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSharedMemory>

#include <memory>
#include <utility>

namespace z7::task_ipc_runtime {

namespace {

bool start_task_ipc_worker_process(
    const QString& worker_program,
    const QStringList& worker_args,
    const QString& working_dir,
    const TaskIpcManagedProcessOptions* process_options,
    QProcess** out_process,
    qint64* out_worker_pid,
    QString* error_message) {
  if (out_worker_pid != nullptr) {
    *out_worker_pid = 0;
  }
  if (out_process != nullptr) {
    *out_process = nullptr;
  }

  if (process_options == nullptr) {
    qint64 worker_pid = 0;
    const bool started = QProcess::startDetached(worker_program, worker_args,
                                                 working_dir, &worker_pid);
    if (!started) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Failed to start task IPC worker process.");
      }
      return false;
    }
    if (out_worker_pid != nullptr) {
      *out_worker_pid = worker_pid;
    }
    return true;
  }

  if (out_process == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing managed task IPC worker process output.");
    }
    return false;
  }

  std::unique_ptr<QProcess> process(new QProcess);
  process->setProgram(worker_program);
  process->setArguments(worker_args);
  process->setWorkingDirectory(working_dir);
  if (process_options->forward_stdin) {
    process->setInputChannelMode(QProcess::ForwardedInputChannel);
  }
  if (process_options->forward_stdout) {
    process->setProcessChannelMode(QProcess::ForwardedOutputChannel);
  }

  process->start();
  if (!process->waitForStarted(30000)) {
    if (error_message != nullptr) {
      const QString detail = process->errorString().trimmed();
      *error_message =
          detail.isEmpty()
              ? QStringLiteral("Failed to start task IPC worker process.")
              : detail;
    }
    return false;
  }

  if (process->processId() <= 0) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC worker started without valid PID.");
    }
    process->kill();
    process->waitForFinished(5000);
    return false;
  }

  if (out_worker_pid != nullptr) {
    *out_worker_pid = process->processId();
  }
  *out_process = process.release();
  return true;
}

bool dispatch_task_ipc_task_impl(
    const QString& worker_program,
    const QString& working_dir,
    const QString& owner_instance_id,
    const TaskIpcPayload& payload,
    const TaskIpcManagedProcessOptions* process_options,
    TaskIpcDispatchResult* out_result,
    QProcess** out_process,
    QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out_process != nullptr) {
    *out_process = nullptr;
  }
  if (out_result == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing task IPC dispatch result output.");
    }
    return false;
  }
  if (worker_program.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC worker program path is empty.");
    }
    return false;
  }
  QString normalized_owner_instance_id;
  if (!validate_task_ipc_owner_instance_id(owner_instance_id,
                                           &normalized_owner_instance_id,
                                           error_message)) {
    return false;
  }

  QString payload_error;
  const QByteArray encoded_payload =
      serialize_task_payload(payload, &payload_error);
  if (encoded_payload.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          payload_error.isEmpty()
              ? QStringLiteral("Failed to serialize task IPC task payload.")
              : payload_error;
    }
    return false;
  }
  if (encoded_payload.size() > kTaskIpcRequestPoolSlotSize) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request payload exceeds fixed request-pool slot capacity.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  const quint64 session_id = next_posix_task_session_id();
  const quint32 generation = 1U;
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      PosixTaskIpcMapping::create_owner(encoded_payload,
                                        normalized_owner_instance_id,
                                        payload.command,
                                        payload.refresh_after_finish,
                                        session_id, generation,
                                        error_message);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return false;
  }
  if (!ensure_task_ipc_semaphore_exists(
          task_ipc_cancel_semaphore_key_for_shm(mapping->shm_name()),
          error_message)) {
    return false;
  }
  if (!mapping->start_owner_waiter(error_message)) {
    return false;
  }
  register_posix_task_mapping(mapping);
  mapping->enable_owner_notification_delivery();
  if (!mapping->start_unclaimed_timer(error_message)) {
    remove_posix_task_mapping(mapping);
    return false;
  }

  TaskIpcPerTaskRaw* raw = mapping->raw();
  QStringList worker_args;
  worker_args << QStringLiteral("--task-ipc-session=%1").arg(session_id)
              << QStringLiteral("--task-ipc-generation=%1").arg(generation)
              << QStringLiteral("--task-ipc-shm=%1").arg(mapping->shm_name())
              << QStringLiteral("--task-ipc-sem=%1").arg(mapping->sem_name());

  qint64 worker_pid = 0;
  if (!start_task_ipc_worker_process(worker_program, worker_args, working_dir,
                                     process_options, out_process,
                                     &worker_pid, error_message)) {
    remove_posix_task_mapping(mapping);
    {
      SharedMemoryLock lock(&raw->lock,
                            task_ipc_per_task_lock_wake_key(mapping->shm_name()));
      if (lock.ok()) {
        clear_slot(&raw->slot, true);
        raw->slot.updated_msecs = now_msecs();
      }
    }
    return false;
  }

  {
    SharedMemoryLock lock(&raw->lock,
                          task_ipc_per_task_lock_wake_key(mapping->shm_name()));
    if (lock.ok() &&
        raw->slot.state == static_cast<quint32>(TaskIpcSlotState::kDispatched)) {
      raw->slot.worker_pid = worker_pid;
      raw->slot.updated_msecs = now_msecs();
    }
  }
  mapping->start_worker_exit_monitor(nullptr);
  post_posix_task_notification(mapping.get(), nullptr);

  out_result->session_id = session_id;
  out_result->generation = generation;
  out_result->worker_pid = worker_pid;
  return true;
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(true, &bootstrap_memory, error_message)) {
    return false;
  }
  std::shared_ptr<QSharedMemory> request_pool_memory;
  if (!open_request_pool_memory(true, &request_pool_memory, error_message)) {
    return false;
  }

  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  TaskIpcRequestPoolHeaderRaw* request_pool =
      request_pool_raw(request_pool_memory.get());
  if (bootstrap == nullptr || request_pool == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC shared memory payload pointer is null.");
    }
    return false;
  }

  int slot_index = -1;
  quint64 session_id = 0;
  quint32 generation = 0;
  const QString bootstrap_lock_wake_key = task_ipc_bootstrap_lock_wake_key();
  {
    SharedMemoryLock bootstrap_lock(&bootstrap->lock, bootstrap_lock_wake_key);
    if (!bootstrap_lock.ok()) {
      if (error_message != nullptr) {
        *error_message = bootstrap_lock.busy()
                             ? QStringLiteral("Task IPC bootstrap is busy.")
                             : bootstrap_lock.error();
      }
      return false;
    }

    reclaim_stale_slots(bootstrap, now_msecs());
    if (bootstrap->next_session_id == 0) {
      bootstrap->next_session_id = 1;
    }

    for (int i = 0; i < kTaskIpcSlotCount; ++i) {
      TaskIpcSlotRaw& slot = bootstrap->slot_records[i];
      if (slot.state == static_cast<quint32>(TaskIpcSlotState::kEmpty)) {
        slot_index = i;
        break;
      }
    }
    if (slot_index < 0) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Task IPC task queue is full.");
      }
      return false;
    }
    TaskIpcSlotRaw& slot = bootstrap->slot_records[slot_index];
    if (slot.generation == 0U) {
      slot.generation = 1U;
    }
    generation = slot.generation;
    session_id = bootstrap->next_session_id;
    if (!ensure_task_ipc_semaphore_exists(
            task_ipc_cancel_semaphore_key_for_task(session_id, generation),
            error_message)) {
      return false;
    }

    SharedMemoryLock slot_lock(
        request_pool_slot_lock(request_pool, slot_index));
    if (!slot_lock.ok()) {
      if (error_message != nullptr) {
        *error_message =
            slot_lock.busy()
                ? QStringLiteral("Task IPC request-pool slot is busy.")
                : slot_lock.error();
      }
      return false;
    }

    if (!write_request_payload_to_slot(request_pool_memory.get(), slot_index,
                                       encoded_payload, error_message)) {
      return false;
    }

    session_id = bootstrap->next_session_id++;
    slot.session_id = session_id;
    slot.state = static_cast<quint32>(TaskIpcSlotState::kDispatched);
    slot.command_kind = static_cast<quint32>(payload.command);
    slot.published_event_sequence = kTaskIpcDispatchedEventSequence;
    slot.acknowledged_event_sequence = 0U;
    slot.result_code = 0;
    slot.refresh_after_finish = payload.refresh_after_finish ? 1U : 0U;
    slot.launcher_pid = static_cast<qint64>(QCoreApplication::applicationPid());
    slot.worker_pid = 0;
    slot.request_pool_slot = static_cast<quint32>(slot_index);
    slot.request_payload_size = static_cast<quint32>(encoded_payload.size());
    slot.updated_msecs = now_msecs();
    write_fixed_utf8(normalized_owner_instance_id, slot.owner_instance_id,
                     kTaskIpcOwnerInstanceIdCapacity);
    write_fixed_utf8(QString(), slot.summary, 256);
  }

  QStringList worker_args;
  worker_args << QStringLiteral("--task-ipc-session=%1").arg(session_id)
              << QStringLiteral("--task-ipc-generation=%1").arg(generation);

  qint64 worker_pid = 0;
  if (!start_task_ipc_worker_process(worker_program, worker_args, working_dir,
                                     process_options, out_process,
                                     &worker_pid, error_message)) {
    SharedMemoryLock bootstrap_lock(&bootstrap->lock, bootstrap_lock_wake_key);
    if (bootstrap_lock.ok()) {
      TaskIpcSlotRaw& slot = bootstrap->slot_records[slot_index];
      if (slot.session_id == session_id && slot.generation == generation) {
        clear_slot(&slot, true);
      }
    }
    return false;
  }
  if (worker_pid <= 0) {
    SharedMemoryLock bootstrap_lock(&bootstrap->lock, bootstrap_lock_wake_key);
    if (bootstrap_lock.ok()) {
      TaskIpcSlotRaw& slot = bootstrap->slot_records[slot_index];
      if (slot.session_id == session_id && slot.generation == generation) {
        clear_slot(&slot, true);
      }
    }
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC worker started without valid PID.");
    }
    return false;
  }

  {
    SharedMemoryLock bootstrap_lock(&bootstrap->lock, bootstrap_lock_wake_key);
    if (bootstrap_lock.ok()) {
      TaskIpcSlotRaw& slot = bootstrap->slot_records[slot_index];
      if (slot.session_id == session_id && slot.generation == generation &&
          slot.state == static_cast<quint32>(TaskIpcSlotState::kDispatched)) {
        slot.worker_pid = worker_pid;
        slot.updated_msecs = now_msecs();
      }
    }
  }
  post_task_ipc_semaphore(
      non_posix_task_ipc_event_semaphore_key(normalized_owner_instance_id),
      nullptr);

  out_result->session_id = session_id;
  out_result->generation = generation;
  out_result->worker_pid = worker_pid;
  return true;
#endif
}

}  // namespace

bool dispatch_task_ipc_task(const QString& worker_program,
                            const QString& working_dir,
                            const QString& owner_instance_id,
                            const TaskIpcPayload& payload,
                            TaskIpcDispatchResult* out_result,
                            QString* error_message) {
  return dispatch_task_ipc_task_impl(worker_program,
                                     working_dir,
                                     owner_instance_id,
                                     payload,
                                     nullptr,
                                     out_result,
                                     nullptr,
                                     error_message);
}

bool dispatch_task_ipc_task_managed_process(
    const QString& worker_program,
    const QString& working_dir,
    const QString& owner_instance_id,
    const TaskIpcPayload& payload,
    const TaskIpcManagedProcessOptions& process_options,
    TaskIpcDispatchResult* out_result,
    QProcess** out_process,
    QString* error_message) {
  return dispatch_task_ipc_task_impl(worker_program,
                                     working_dir,
                                     owner_instance_id,
                                     payload,
                                     &process_options,
                                     out_result,
                                     out_process,
                                     error_message);
}

bool request_task_ipc_cancel(quint64 session_id,
                             quint32 generation,
                             QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (session_id == 0 || generation == 0U) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC cancel requires session and generation.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  const std::shared_ptr<PosixTaskIpcMapping> mapping =
      find_posix_task_mapping(session_id, generation);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return true;
  }

  TaskIpcSlotRaw& slot = mapping->raw()->slot;
  if (slot.session_id != session_id || slot.generation != generation) {
    return true;
  }

  const std::atomic_ref<quint32> state(slot.state);
  const quint32 state_value = state.load(std::memory_order_acquire);
  if (state_value == static_cast<quint32>(TaskIpcSlotState::kEmpty) ||
      state_value == static_cast<quint32>(TaskIpcSlotState::kCompleted)) {
    return true;
  }

  std::atomic_ref<quint32> cancel_requested(slot.cancel_requested);
  std::atomic_ref<qint64> updated(slot.updated_msecs);
  cancel_requested.store(1U, std::memory_order_release);
  updated.store(now_msecs(), std::memory_order_release);
  return post_task_ipc_semaphore(
      task_ipc_cancel_semaphore_key_for_shm(mapping->shm_name()),
      error_message);
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

  reclaim_stale_slots(bootstrap, now_msecs());
  for (int i = 0; i < kTaskIpcSlotCount; ++i) {
    TaskIpcSlotRaw& slot = bootstrap->slot_records[i];
    if (slot.session_id != session_id || slot.generation != generation) {
      continue;
    }
    if (slot.state == static_cast<quint32>(TaskIpcSlotState::kEmpty) ||
        slot.state == static_cast<quint32>(TaskIpcSlotState::kCompleted)) {
      return true;
    }
    slot.cancel_requested = 1U;
    slot.updated_msecs = now_msecs();
    return post_task_ipc_semaphore(
        task_ipc_cancel_semaphore_key_for_task(slot.session_id,
                                               slot.generation),
        error_message);
  }
  return true;
#endif
}

bool claim_task_ipc_task_for_worker(quint64 session_id, quint32 generation,
                                    TaskIpcClaimedTask* out_task,
                                    QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out_task == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing task IPC claimed task output.");
    }
    return false;
  }
  *out_task = TaskIpcClaimedTask{};

  if (session_id == 0 || generation == 0U) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC claim requires session and generation.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      PosixTaskIpcMapping::open_worker(posix_worker_shm_name(),
                                       posix_worker_sem_name(), error_message);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return false;
  }

  QByteArray payload_bytes;
  {
    TaskIpcPerTaskRaw* raw = mapping->raw();
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
    if (slot.session_id != session_id || slot.generation != generation ||
        slot.state != static_cast<quint32>(TaskIpcSlotState::kDispatched)) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Requested task IPC task is no longer available.");
      }
      return false;
    }

    slot.state = static_cast<quint32>(TaskIpcSlotState::kClaimed);
    slot.worker_pid = static_cast<qint64>(QCoreApplication::applicationPid());
    static_cast<void>(publish_task_ipc_stage_event(
        &slot, TaskIpcEventKind::kClaimed, now_msecs()));

    out_task->slot_index = 0;
    out_task->session_id = slot.session_id;
    out_task->generation = slot.generation;
    out_task->ipc_shm_name = mapping->shm_name();
    out_task->ipc_sem_name = mapping->sem_name();
    out_task->owner_instance_id =
        read_fixed_utf8(slot.owner_instance_id,
                        kTaskIpcOwnerInstanceIdCapacity);
    out_task->launcher_pid = slot.launcher_pid;
    out_task->worker_pid = slot.worker_pid;
    payload_bytes =
        QByteArray(raw->payload, static_cast<int>(slot.request_payload_size));
  }

  TaskIpcPayload decoded_payload;
  QString read_error;
  if (!deserialize_task_payload(payload_bytes, &decoded_payload, &read_error)) {
    decoded_payload.command = TaskIpcCommandKind::kNone;
    decoded_payload.caption =
        read_error.isEmpty()
            ? QStringLiteral("Invalid task IPC request payload.")
            : read_error;
  }
  out_task->payload = std::move(decoded_payload);
  register_posix_task_mapping(mapping);
  mapping->stop_unclaimed_timer();
  mapping->start_worker_exit_monitor(nullptr);
  post_posix_task_notification(mapping.get(), nullptr);
  return true;
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!open_bootstrap_memory(false, &bootstrap_memory, error_message)) {
    return false;
  }
  std::shared_ptr<QSharedMemory> request_pool_memory;
  if (!open_request_pool_memory(false, &request_pool_memory, error_message)) {
    return false;
  }

  TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
  TaskIpcRequestPoolHeaderRaw* request_pool =
      request_pool_raw(request_pool_memory.get());
  if (bootstrap == nullptr || request_pool == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC shared memory payload pointer is null.");
    }
    return false;
  }

  {
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

    reclaim_stale_slots(bootstrap, now_msecs());

    int slot_index = -1;
    TaskIpcSlotRaw* slot = nullptr;
    for (int i = 0; i < kTaskIpcSlotCount; ++i) {
      TaskIpcSlotRaw& candidate = bootstrap->slot_records[i];
      if (candidate.session_id == session_id &&
          candidate.generation == generation &&
          candidate.state ==
              static_cast<quint32>(TaskIpcSlotState::kDispatched)) {
        slot_index = i;
        slot = &candidate;
        break;
      }
    }
    if (slot == nullptr) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Requested task IPC task is no longer available.");
      }
      return false;
    }

    SharedMemoryLock slot_lock(request_pool_slot_lock(request_pool, slot_index));
    if (!slot_lock.ok()) {
      if (error_message != nullptr) {
        *error_message =
            slot_lock.busy()
                ? QStringLiteral("Task IPC request-pool slot is busy.")
                : slot_lock.error();
      }
      return false;
    }

    slot->state = static_cast<quint32>(TaskIpcSlotState::kClaimed);
    slot->worker_pid = static_cast<qint64>(QCoreApplication::applicationPid());
    static_cast<void>(publish_task_ipc_stage_event(
        slot, TaskIpcEventKind::kClaimed, now_msecs()));

    out_task->slot_index = slot_index;
    out_task->session_id = slot->session_id;
    out_task->generation = slot->generation;
    out_task->owner_instance_id =
        read_fixed_utf8(slot->owner_instance_id,
                        kTaskIpcOwnerInstanceIdCapacity);
    out_task->launcher_pid = slot->launcher_pid;
    out_task->worker_pid = slot->worker_pid;

    TaskIpcPayload payload;
    QString read_error;
    if (!read_request_payload_from_slot(
            request_pool_memory.get(), static_cast<int>(slot->request_pool_slot),
            slot->request_payload_size, &payload, &read_error)) {
      payload.command = TaskIpcCommandKind::kNone;
      payload.caption = read_error.isEmpty()
                            ? QStringLiteral("Invalid task IPC request payload.")
                            : read_error;
    }
    out_task->payload = std::move(payload);
  }
  post_task_ipc_semaphore(
      non_posix_task_ipc_event_semaphore_key(out_task->owner_instance_id),
      nullptr);
  return true;
#endif
}

bool query_task_ipc_cancel_requested(const TaskIpcClaimedTask& task,
                                     bool* out_canceled,
                                     QString* error_message) {
  using namespace task_ipc_internal;

  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out_canceled == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing task IPC cancel status output.");
    }
    return false;
  }
  *out_canceled = false;

  if (task.session_id == 0 || task.generation == 0U) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC cancel query requires claimed task identity.");
    }
    return false;
  }

#if defined(Q_OS_MACOS)
  std::shared_ptr<PosixTaskIpcMapping> mapping =
      find_posix_task_mapping(task.session_id, task.generation);
  if (mapping == nullptr || mapping->raw() == nullptr) {
    QString open_error;
    mapping = PosixTaskIpcMapping::open_worker(task.ipc_shm_name,
                                               task.ipc_sem_name,
                                               &open_error);
    if (mapping == nullptr || mapping->raw() == nullptr) {
      if (error_message != nullptr && !open_error.trimmed().isEmpty()) {
        *error_message = open_error;
      }
      return true;
    }
    register_posix_task_mapping(mapping);
  }

  TaskIpcSlotRaw& slot = mapping->raw()->slot;
  if (!slot_matches_claim(slot, task)) {
    return true;
  }
  const std::atomic_ref<quint32> cancel_requested(slot.cancel_requested);
  *out_canceled =
      cancel_requested.load(std::memory_order_acquire) != 0U;
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

  if (task.slot_index < 0 || task.slot_index >= kTaskIpcSlotCount) {
    return true;
  }
  const TaskIpcSlotRaw& slot = bootstrap->slot_records[task.slot_index];
  if (!slot_matches_claim(slot, task)) {
    return true;
  }
  *out_canceled = slot.cancel_requested != 0U;
  return true;
#endif
}

}  // namespace z7::task_ipc_runtime
