#include "task_ipc_runtime_internal.h"

#include <QCoreApplication>
#include <QNativeIpcKey>
#include <QSharedMemory>
#include <QVariant>
#include <QUuid>

#include <cstring>
#include <utility>

#ifndef Q_OS_WIN
#include <sys/mman.h>
#endif

namespace z7::task_ipc_runtime::task_ipc_internal {
namespace {

#if !defined(Q_OS_MACOS)
void set_memory_native_key(QSharedMemory* memory, const QString& native_key) {
  if (memory != nullptr) {
#ifdef Q_OS_WIN
    memory->setNativeKey(native_key, QNativeIpcKey::Type::Windows);
#elif !defined(Q_OS_MACOS)
    memory->setNativeKey(native_key, QNativeIpcKey::Type::PosixRealtime);
#else
    Q_UNUSED(native_key);
#endif
  }
}

void remove_stale_named_memory(const QString& native_key) {
#if defined(Q_OS_MACOS)
  Q_UNUSED(native_key);
#elif !defined(Q_OS_WIN)
  const QByteArray encoded_key = native_key.toUtf8();
  ::shm_unlink(encoded_key.constData());
#else
  QSharedMemory memory;
  set_memory_native_key(&memory, native_key);
  if (memory.attach(QSharedMemory::ReadWrite)) {
    memory.detach();
  }
#endif
}

bool create_or_attach_shared_memory(QSharedMemory* memory, bool allow_create,
                                    int size, const QString& create_failure,
                                    const QString& attach_failure,
                                    bool* out_created, QString* error_message) {
  if (out_created != nullptr) {
    *out_created = false;
  }
  if (memory == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC shared memory handle is null.");
    }
    return false;
  }

  if (allow_create) {
    if (memory->create(size, QSharedMemory::ReadWrite)) {
      if (out_created != nullptr) {
        *out_created = true;
      }
      return true;
    }
    if (memory->error() != QSharedMemory::AlreadyExists) {
      if (error_message != nullptr) {
        *error_message =
            error_string_from_shared_memory(memory, create_failure);
      }
      return false;
    }
  }

  if (memory->attach(QSharedMemory::ReadWrite)) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message = error_string_from_shared_memory(memory, attach_failure);
  }
  return false;
}
#endif

}  // namespace

bool initialize_bootstrap_if_needed(QSharedMemory* memory, bool force_init,
                                    QString* error_message) {
  TaskIpcBootstrapRaw* raw = bootstrap_raw(memory);
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC bootstrap shared memory is null.");
    }
    return false;
  }
  if (memory->size() < static_cast<int>(sizeof(TaskIpcBootstrapRaw))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
                           "Task IPC bootstrap shared memory size is invalid "
                           "(actual=%1 expected=%2).")
                           .arg(memory->size())
                           .arg(sizeof(TaskIpcBootstrapRaw));
    }
    return false;
  }

  if (force_init) {
    std::memset(raw, 0, sizeof(TaskIpcBootstrapRaw));
    raw->lock.locker_pid = 0;
    raw->magic = kTaskIpcMagic;
    raw->version = kTaskIpcVersion;
    raw->slot_count = static_cast<quint16>(kTaskIpcSlotCount);
    raw->next_session_id = 1;
    for (int i = 0; i < kTaskIpcSlotCount; ++i) {
      raw->slot_records[i].generation = 1;
      raw->slot_records[i].state =
          static_cast<quint32>(TaskIpcSlotState::kEmpty);
      raw->slot_records[i].published_event_sequence = 0U;
      raw->slot_records[i].acknowledged_event_sequence = 0U;
    }
    return true;
  }

  if (raw->magic != kTaskIpcMagic || raw->version != kTaskIpcVersion ||
      raw->slot_count != static_cast<quint16>(kTaskIpcSlotCount)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC bootstrap shared memory header is invalid.");
    }
    return false;
  }
  return true;
}

bool initialize_request_pool_if_needed(QSharedMemory* memory, bool force_init,
                                       QString* error_message) {
  TaskIpcRequestPoolHeaderRaw* raw = request_pool_raw(memory);
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool shared memory is null.");
    }
    return false;
  }
  if (memory->size() < kTaskIpcRequestPoolSharedMemorySize) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral(
              "Task IPC request-pool shared memory size is invalid "
              "(actual=%1 expected=%2).")
              .arg(memory->size())
              .arg(kTaskIpcRequestPoolSharedMemorySize);
    }
    return false;
  }
  if (force_init) {
    std::memset(raw, 0,
                static_cast<size_t>(kTaskIpcRequestPoolSharedMemorySize));
    raw->magic = kTaskIpcRequestPoolMagic;
    raw->version = kTaskIpcRequestPoolVersion;
    raw->slot_count = static_cast<quint16>(kTaskIpcSlotCount);
    return true;
  }

  if (raw->magic != kTaskIpcRequestPoolMagic ||
      raw->version != kTaskIpcRequestPoolVersion ||
      raw->slot_count != static_cast<quint16>(kTaskIpcSlotCount)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request-pool shared memory header is invalid.");
    }
    return false;
  }
  return true;
}

bool open_bootstrap_memory(bool allow_create,
                           std::shared_ptr<QSharedMemory>* out_memory,
                           QString* error_message) {
#if defined(Q_OS_MACOS)
  Q_UNUSED(allow_create);
  Q_UNUSED(out_memory);
  if (error_message != nullptr) {
    *error_message = QStringLiteral(
        "Task IPC bootstrap shared memory is not used on macOS.");
  }
  return false;
#else
  if (out_memory == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing task IPC bootstrap output storage.");
    }
    return false;
  }

  const std::shared_ptr<QSharedMemory> leased_memory =
      current_bootstrap_memory_lease();
  if (leased_memory != nullptr && leased_memory->isAttached() &&
      leased_memory->size() >= static_cast<int>(sizeof(TaskIpcBootstrapRaw))) {
    *out_memory = leased_memory;
    return true;
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    auto memory = std::make_shared<QSharedMemory>();
    set_memory_native_key(memory.get(), task_ipc_bootstrap_key());
    bool created = false;
    if (!create_or_attach_shared_memory(
            memory.get(), allow_create,
            static_cast<int>(sizeof(TaskIpcBootstrapRaw)),
            QStringLiteral(
                "Failed to create task IPC bootstrap shared memory."),
            QStringLiteral(
                "Failed to attach task IPC bootstrap shared memory."),
            &created, error_message)) {
      return false;
    }

    QString init_error;
    if (initialize_bootstrap_if_needed(memory.get(), created, &init_error)) {
      update_bootstrap_memory_lease(memory);
      *out_memory = std::move(memory);
      return true;
    }

    if (!allow_create || attempt != 0) {
      if (error_message != nullptr) {
        *error_message = init_error;
      }
      return false;
    }

    if (memory->isAttached()) {
      memory->detach();
    }
    remove_stale_named_memory(task_ipc_bootstrap_key());
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral(
        "Failed to initialize task IPC bootstrap shared memory.");
  }
  return false;
#endif
}

bool open_request_pool_memory(bool allow_create,
                              std::shared_ptr<QSharedMemory>* out_memory,
                              QString* error_message) {
#if defined(Q_OS_MACOS)
  Q_UNUSED(allow_create);
  Q_UNUSED(out_memory);
  if (error_message != nullptr) {
    *error_message = QStringLiteral(
        "Task IPC request-pool shared memory is not used on macOS.");
  }
  return false;
#else
  if (out_memory == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing task IPC request-pool output storage.");
    }
    return false;
  }

  const std::shared_ptr<QSharedMemory> leased_memory =
      current_request_pool_memory_lease();
  if (leased_memory != nullptr && leased_memory->isAttached() &&
      leased_memory->size() >= kTaskIpcRequestPoolSharedMemorySize) {
    *out_memory = leased_memory;
    return true;
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    auto memory = std::make_shared<QSharedMemory>();
    set_memory_native_key(memory.get(), task_ipc_request_pool_key());
    bool created = false;
    if (!create_or_attach_shared_memory(
            memory.get(), allow_create, kTaskIpcRequestPoolSharedMemorySize,
            QStringLiteral(
                "Failed to create task IPC request-pool shared memory."),
            QStringLiteral(
                "Failed to attach task IPC request-pool shared memory."),
            &created, error_message)) {
      return false;
    }

    QString init_error;
    if (initialize_request_pool_if_needed(memory.get(), created, &init_error)) {
      update_request_pool_memory_lease(memory);
      *out_memory = std::move(memory);
      return true;
    }

    if (!allow_create || attempt != 0) {
      if (error_message != nullptr) {
        *error_message = init_error;
      }
      return false;
    }

    if (memory->isAttached()) {
      memory->detach();
    }
    remove_stale_named_memory(task_ipc_request_pool_key());
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral(
        "Failed to initialize task IPC request-pool shared memory.");
  }
  return false;
#endif
}

}  // namespace z7::task_ipc_runtime::task_ipc_internal

namespace z7::task_ipc_runtime {

QString task_ipc_bootstrap_key() {
#ifdef Q_OS_WIN
  return QStringLiteral("Local\\z7.bridge.bootstrap.v1");
#elif defined(Q_OS_MACOS)
  return QString();
#else
  return QStringLiteral("/z7.bridge.bootstrap.v1");
#endif
}

QString task_ipc_request_pool_key() {
#ifdef Q_OS_WIN
  return QStringLiteral("Local\\z7.bridge.reqpool.v1");
#elif defined(Q_OS_MACOS)
  return QString();
#else
  return QStringLiteral("/z7.bridge.reqpool.v1");
#endif
}

QString ensure_task_ipc_owner_instance_id() {
  QCoreApplication* app = QCoreApplication::instance();
  if (app == nullptr) {
    return QStringLiteral("task-ipc-owner-unknown");
  }
  const QVariant existing =
      app->property(task_ipc_internal::kTaskIpcOwnerIdProperty);
  const QString value = existing.toString().trimmed();
  if (!value.isEmpty()) {
    return value;
  }
  const QString generated = QUuid::createUuid().toString(QUuid::WithoutBraces);
  app->setProperty(task_ipc_internal::kTaskIpcOwnerIdProperty, generated);
  return generated;
}

bool ensure_task_ipc_bootstrap_ready(QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

#if defined(Q_OS_MACOS)
  return true;
#else
  std::shared_ptr<QSharedMemory> bootstrap_memory;
  if (!task_ipc_internal::open_bootstrap_memory(true, &bootstrap_memory,
                                                error_message)) {
    return false;
  }

  std::shared_ptr<QSharedMemory> request_pool_memory;
  return task_ipc_internal::open_request_pool_memory(true, &request_pool_memory,
                                                     error_message);
#endif
}

void set_task_ipc_worker_endpoint(const QString& shm_name,
                                  const QString& sem_name) {
#if defined(Q_OS_MACOS)
  task_ipc_internal::set_posix_worker_endpoint(shm_name, sem_name);
#else
  Q_UNUSED(shm_name);
  Q_UNUSED(sem_name);
#endif
}

bool set_task_ipc_event_notifier(
    const QString& owner_instance_id,
    TaskIpcEventNotifier notifier,
    QString* error_message) {
  QString normalized_owner_instance_id;
  if (!task_ipc_internal::validate_task_ipc_owner_instance_id(
          owner_instance_id, &normalized_owner_instance_id, error_message)) {
    return false;
  }
#if defined(Q_OS_MACOS)
  task_ipc_internal::set_posix_event_notifier(
      normalized_owner_instance_id, std::move(notifier));
  return true;
#else
  return task_ipc_internal::set_non_posix_event_notifier(
      normalized_owner_instance_id, std::move(notifier), error_message);
#endif
}

bool clear_task_ipc_event_notifier(const QString& owner_instance_id,
                                   QString* error_message) {
  QString normalized_owner_instance_id;
  if (!task_ipc_internal::validate_task_ipc_owner_instance_id(
          owner_instance_id, &normalized_owner_instance_id, error_message)) {
    return false;
  }
#if defined(Q_OS_MACOS)
  task_ipc_internal::clear_posix_event_notifier(normalized_owner_instance_id);
  return true;
#else
  task_ipc_internal::clear_non_posix_event_notifier(normalized_owner_instance_id);
  return true;
#endif
}

bool set_task_ipc_cancel_notifier(const TaskIpcClaimedTask& task,
                                  TaskIpcCancelNotifier notifier,
                                  QString* error_message) {
  return task_ipc_internal::start_task_ipc_cancel_notification_thread(
      task, std::move(notifier), error_message);
}

}  // namespace z7::task_ipc_runtime
