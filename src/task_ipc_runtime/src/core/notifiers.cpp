#include "internal.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QHash>

#include <chrono>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace z7::task_ipc_runtime::task_ipc_internal {
namespace {

constexpr auto kNonPosixOwnerMonitorPollInterval =
    std::chrono::milliseconds(100);

QString hashed_task_ipc_semaphore_key(const QString& prefix,
                                      const QString& value) {
  const QByteArray hash =
      QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("%1%2")
      .arg(prefix, QString::fromLatin1(hash.constData(),
                                       std::min<int>(hash.size(), 24)));
}

bool initialize_task_ipc_semaphore(QSystemSemaphore* semaphore,
                                   const QString& key,
                                   QSystemSemaphore::AccessMode mode,
                                   QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (semaphore == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC semaphore handle is null.");
    }
    return false;
  }

  semaphore->setNativeKey(QSystemSemaphore::platformSafeKey(key), 0, mode);
  if (semaphore->error() == QSystemSemaphore::NoError) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message =
        semaphore->errorString().trimmed().isEmpty()
            ? QStringLiteral("Failed to initialize task IPC semaphore.")
            : semaphore->errorString().trimmed();
  }
  return false;
}

std::mutex& non_posix_event_notifier_mutex() {
  static std::mutex mutex;
  return mutex;
}

QHash<QString, TaskIpcEventNotifier>& non_posix_event_notifiers() {
  static QHash<QString, TaskIpcEventNotifier> notifiers;
  return notifiers;
}

class NonPosixEventWaiter;

QHash<QString, std::shared_ptr<NonPosixEventWaiter>>& non_posix_event_waiters();

class NonPosixEventWaiter {
 public:
  explicit NonPosixEventWaiter(QString owner_instance_id)
      : owner_instance_id_(std::move(owner_instance_id)),
        semaphore_(QNativeIpcKey()) {}

  ~NonPosixEventWaiter() { stop(); }

  NonPosixEventWaiter(const NonPosixEventWaiter&) = delete;
  NonPosixEventWaiter& operator=(const NonPosixEventWaiter&) = delete;

  bool start(QString* error_message) {
    if (started_) {
      return true;
    }
    if (!initialize_task_ipc_semaphore(
            &semaphore_, non_posix_task_ipc_event_semaphore_key(owner_instance_id_),
            QSystemSemaphore::Create,
            error_message)) {
      return false;
    }

    stop_requested_.store(false, std::memory_order_release);
    try {
      waiter_thread_ = std::thread([this]() { wait_loop(); });
      monitor_thread_ = std::thread([this]() { monitor_loop(); });
    } catch (const std::system_error& error) {
      stop_requested_.store(true, std::memory_order_release);
      QString ignored_error;
      post_task_ipc_semaphore(
          non_posix_task_ipc_event_semaphore_key(owner_instance_id_),
          &ignored_error);
      if (waiter_thread_.joinable()) {
        waiter_thread_.join();
      }
      if (monitor_thread_.joinable()) {
        monitor_thread_.join();
      }
      if (error_message != nullptr) {
        *error_message = QStringLiteral(
            "Failed to start task IPC event waiter: %1")
                             .arg(QString::fromLocal8Bit(error.what()));
      }
      return false;
    }
    started_ = true;
    return true;
  }

  void stop() {
    if (!started_) {
      return;
    }

    stop_requested_.store(true, std::memory_order_release);
    QString ignored_error;
    post_task_ipc_semaphore(
        non_posix_task_ipc_event_semaphore_key(owner_instance_id_),
        &ignored_error);
    if (waiter_thread_.joinable()) {
      waiter_thread_.join();
    }
    if (monitor_thread_.joinable()) {
      monitor_thread_.join();
    }
    started_ = false;
  }

 private:
  void monitor_loop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
      scan_owner_events();
      std::this_thread::sleep_for(kNonPosixOwnerMonitorPollInterval);
    }
  }

  void scan_owner_events() {
    std::shared_ptr<QSharedMemory> bootstrap_memory;
    QString bootstrap_error;
    if (!open_bootstrap_memory(false, &bootstrap_memory, &bootstrap_error)) {
      return;
    }

    TaskIpcBootstrapRaw* bootstrap = bootstrap_raw(bootstrap_memory.get());
    if (bootstrap == nullptr) {
      return;
    }

    bool should_notify = false;
    const qint64 now = now_msecs();
    SharedMemoryLock lock(&bootstrap->lock, task_ipc_bootstrap_lock_wake_key());
    if (!lock.ok()) {
      return;
    }

    reclaim_stale_slots(bootstrap, now);
    for (int i = 0; i < kTaskIpcSlotCount; ++i) {
      TaskIpcSlotRaw& slot = bootstrap->slot_records[i];
      if (!slot_matches_owner(slot, owner_instance_id_)) {
        continue;
      }
      should_notify |=
          publish_task_ipc_unclaimed_timeout_completion(&slot, now);
      should_notify |= publish_task_ipc_worker_exit_completion(&slot, now);
    }

    if (should_notify) {
      post_task_ipc_semaphore(
          non_posix_task_ipc_event_semaphore_key(owner_instance_id_), nullptr);
    }
  }

  void wait_loop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
      if (!semaphore_.acquire()) {
        return;
      }
      if (stop_requested_.load(std::memory_order_acquire)) {
        return;
      }

      TaskIpcEventNotifier notifier;
      {
        std::lock_guard<std::mutex> lock(non_posix_event_notifier_mutex());
        notifier = non_posix_event_notifiers().value(owner_instance_id_);
      }
      if (notifier) {
        try {
          notifier(owner_instance_id_);
        } catch (...) {
        }
      }
    }
  }

  QString owner_instance_id_;
  QSystemSemaphore semaphore_;
  bool started_ = false;
  std::atomic_bool stop_requested_{false};
  std::thread waiter_thread_;
  std::thread monitor_thread_;
};

QHash<QString, std::shared_ptr<NonPosixEventWaiter>>& non_posix_event_waiters() {
  static QHash<QString, std::shared_ptr<NonPosixEventWaiter>> waiters;
  return waiters;
}

}  // namespace

QString non_posix_task_ipc_event_semaphore_key(const QString& owner_instance_id) {
  return hashed_task_ipc_semaphore_key(QStringLiteral("z7.taskipc.owner."),
                                       owner_instance_id.trimmed());
}

QString task_ipc_cancel_semaphore_key_for_task(quint64 session_id,
                                               quint32 generation) {
  return hashed_task_ipc_semaphore_key(
      QStringLiteral("z7.taskipc.cancel.task."),
      QStringLiteral("%1.%2").arg(session_id).arg(generation));
}

QString task_ipc_cancel_semaphore_key_for_shm(const QString& shm_name) {
  return hashed_task_ipc_semaphore_key(QStringLiteral("z7.taskipc.cancel.shm."),
                                       shm_name.trimmed());
}

QString task_ipc_cancel_semaphore_key(const TaskIpcClaimedTask& task) {
#if defined(Q_OS_MACOS)
  if (!task.ipc_shm_name.trimmed().isEmpty()) {
    return task_ipc_cancel_semaphore_key_for_shm(task.ipc_shm_name);
  }
#endif
  return task_ipc_cancel_semaphore_key_for_task(task.session_id,
                                                task.generation);
}

bool ensure_task_ipc_semaphore_exists(const QString& key,
                                      QString* error_message) {
  QSystemSemaphore semaphore{QNativeIpcKey()};
  return initialize_task_ipc_semaphore(
      &semaphore, key, QSystemSemaphore::Create, error_message);
}

bool post_task_ipc_semaphore(const QString& key, QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  QSystemSemaphore semaphore{QNativeIpcKey()};
  if (!initialize_task_ipc_semaphore(
          &semaphore, key, QSystemSemaphore::Open, error_message)) {
    return false;
  }
  if (semaphore.release()) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message =
        semaphore.errorString().trimmed().isEmpty()
            ? QStringLiteral("Failed to release task IPC semaphore.")
            : semaphore.errorString().trimmed();
  }
  return false;
}

bool set_non_posix_event_notifier(const QString& owner_instance_id,
                                  TaskIpcEventNotifier notifier,
                                  QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  const QString trimmed_owner = owner_instance_id.trimmed();
  if (trimmed_owner.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC owner instance id is empty.");
    }
    return false;
  }
  if (!notifier) {
    clear_non_posix_event_notifier(trimmed_owner);
    return true;
  }

  std::shared_ptr<NonPosixEventWaiter> waiter;
  bool should_start_waiter = false;
  {
    std::lock_guard<std::mutex> lock(non_posix_event_notifier_mutex());
    non_posix_event_notifiers().insert(trimmed_owner, std::move(notifier));
    std::shared_ptr<NonPosixEventWaiter>& waiter_slot =
        non_posix_event_waiters()[trimmed_owner];
    if (waiter_slot == nullptr) {
      waiter_slot = std::make_shared<NonPosixEventWaiter>(trimmed_owner);
      waiter = waiter_slot;
      should_start_waiter = true;
    }
  }

  if (!should_start_waiter) {
    return true;
  }
  if (waiter == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC event waiter was not created.");
    }
    return false;
  }
  if (waiter->start(error_message)) {
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(non_posix_event_notifier_mutex());
    if (non_posix_event_waiters().value(trimmed_owner) == waiter) {
      non_posix_event_waiters().remove(trimmed_owner);
      non_posix_event_notifiers().remove(trimmed_owner);
    }
  }
  return false;
}

void clear_non_posix_event_notifier(const QString& owner_instance_id) {
  const QString trimmed_owner = owner_instance_id.trimmed();
  if (trimmed_owner.isEmpty()) {
    return;
  }

  std::shared_ptr<NonPosixEventWaiter> waiter_to_stop;
  {
    std::lock_guard<std::mutex> lock(non_posix_event_notifier_mutex());
    non_posix_event_notifiers().remove(trimmed_owner);
    waiter_to_stop = non_posix_event_waiters().take(trimmed_owner);
  }

  if (waiter_to_stop != nullptr) {
    waiter_to_stop->stop();
  }
}

bool start_task_ipc_cancel_notification_thread(
    const TaskIpcClaimedTask& task, TaskIpcCancelNotifier notifier,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!notifier) {
    return true;
  }

  bool canceled = false;
  if (!z7::task_ipc_runtime::query_task_ipc_cancel_requested(task, &canceled,
                                                             error_message)) {
    return false;
  }
  if (canceled) {
    notifier();
    return true;
  }

  const QString semaphore_key = task_ipc_cancel_semaphore_key(task);
  if (semaphore_key.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC cancel semaphore key is empty.");
    }
    return false;
  }

  try {
    std::thread([task, semaphore_key, notifier = std::move(notifier)]() mutable {
      QSystemSemaphore semaphore{QNativeIpcKey()};
      QString semaphore_error;
      if (!initialize_task_ipc_semaphore(
              &semaphore, semaphore_key, QSystemSemaphore::Open,
              &semaphore_error)) {
        return;
      }

      for (;;) {
        if (!semaphore.acquire()) {
          return;
        }

        bool canceled_local = false;
        QString cancel_error;
        if (!z7::task_ipc_runtime::query_task_ipc_cancel_requested(
                task, &canceled_local, &cancel_error)) {
          return;
        }
        if (canceled_local) {
          notifier();
          return;
        }
      }
    }).detach();
  } catch (const std::system_error& error) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Failed to start task IPC cancel waiter: %1")
                           .arg(QString::fromLocal8Bit(error.what()));
    }
    return false;
  }
  return true;
}

}  // namespace z7::task_ipc_runtime::task_ipc_internal
