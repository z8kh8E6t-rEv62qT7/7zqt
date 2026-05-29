#include "internal.h"

#if defined(Q_OS_MACOS)

#include <QCoreApplication>
#include <QHash>
#include <QUuid>

#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace z7::task_ipc_runtime::task_ipc_internal {
namespace {

std::mutex& posix_task_registry_mutex() {
  static std::mutex mutex;
  return mutex;
}

QVector<std::shared_ptr<PosixTaskIpcMapping>>& posix_task_registry() {
  static QVector<std::shared_ptr<PosixTaskIpcMapping>> registry;
  return registry;
}

std::mutex& posix_worker_endpoint_mutex() {
  static std::mutex mutex;
  return mutex;
}

QString& posix_worker_shm_name_storage() {
  static QString name;
  return name;
}

QString& posix_worker_sem_name_storage() {
  static QString name;
  return name;
}

std::mutex& posix_event_notifier_mutex() {
  static std::mutex mutex;
  return mutex;
}

QHash<QString, TaskIpcEventNotifier>& posix_event_notifiers() {
  static QHash<QString, TaskIpcEventNotifier> notifiers;
  return notifiers;
}

QString posix_errno_message(const QString& operation) {
  return QStringLiteral("%1 failed: %2")
      .arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

QString short_posix_suffix() {
  const QString uuid =
      QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  const qint64 pid = QCoreApplication::applicationPid();
  return QStringLiteral("%1-%2").arg(pid % 100000).arg(uuid);
}

QByteArray posix_name_bytes(const QString& name) {
  return name.toUtf8();
}

bool cleanup_posix_unlink(const QString& operation, const QByteArray& name,
                          const QString& display_name,
                          int (*unlink_fn)(const char*)) {
  Q_UNUSED(operation);
  Q_UNUSED(display_name);
  if (name.isEmpty()) {
    return true;
  }
  if (unlink_fn(name.constData()) == 0) {
    return true;
  }
  const int error_number = errno;
  if (error_number == ENOENT) {
    return true;
  }
  return false;
}

void cleanup_posix_munmap(void* mapping, const QString& display_name) {
  Q_UNUSED(display_name);
  if (mapping == nullptr || mapping == MAP_FAILED) {
    return;
  }
  ::munmap(mapping, sizeof(TaskIpcPerTaskRaw));
}

void cleanup_posix_close(int fd, const QString& display_name) {
  Q_UNUSED(display_name);
  if (fd == -1) {
    return;
  }
  ::close(fd);
}

void cleanup_posix_sem_close(sem_t* semaphore, const QString& display_name) {
  Q_UNUSED(display_name);
  if (semaphore == nullptr || semaphore == SEM_FAILED) {
    return;
  }
  ::sem_close(semaphore);
}

class PosixEventDispatcher {
 public:
  PosixEventDispatcher() = default;
  ~PosixEventDispatcher() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  PosixEventDispatcher(const PosixEventDispatcher&) = delete;
  PosixEventDispatcher& operator=(const PosixEventDispatcher&) = delete;

  void enqueue(const QString& owner_instance_id) {
    const QString trimmed_owner = owner_instance_id.trimmed();
    if (trimmed_owner.isEmpty()) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      if (!thread_.joinable()) {
        try {
          thread_ = std::thread([this]() { dispatch_loop(); });
        } catch (const std::system_error&) {
          return;
        }
      }
      try {
        pending_.push_back(trimmed_owner);
      } catch (const std::exception&) {
        return;
      }
    }
    condition_.notify_one();
  }

 private:
  void dispatch_loop() {
    for (;;) {
      QString owner_instance_id;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() {
          return stopping_ || !pending_.empty();
        });
        if (stopping_ && pending_.empty()) {
          return;
        }
        owner_instance_id = pending_.front();
        pending_.pop_front();
      }

      TaskIpcEventNotifier notifier =
          posix_event_notifier_for_owner(owner_instance_id);
      if (notifier) {
        try {
          notifier(owner_instance_id);
        } catch (...) {
        }
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<QString> pending_;
  std::thread thread_;
  bool stopping_ = false;
};

PosixEventDispatcher& posix_event_dispatcher() {
  static_cast<void>(posix_event_notifier_mutex());
  static_cast<void>(posix_event_notifiers());
  static PosixEventDispatcher dispatcher;
  return dispatcher;
}

}  // namespace

PosixTaskIpcMapping::PosixTaskIpcMapping(QString shm_name, QString sem_name,
                                         int fd, void* mapping,
                                         sem_t* semaphore,
                                         QString owner_instance_id,
                                         bool unlink_on_destroy)
    : shm_name_(std::move(shm_name)),
      sem_name_(std::move(sem_name)),
      owner_instance_id_(std::move(owner_instance_id)),
      fd_(fd),
      mapping_(mapping),
      semaphore_(semaphore),
      unlink_on_destroy_(unlink_on_destroy) {}

PosixTaskIpcMapping::~PosixTaskIpcMapping() {
  stop_owner_waiter();

  if (mapping_ != nullptr && mapping_ != MAP_FAILED) {
    cleanup_posix_munmap(mapping_, shm_name_);
    mapping_ = nullptr;
  }
  if (fd_ != -1) {
    cleanup_posix_close(fd_, shm_name_);
    fd_ = -1;
  }
  if (!semaphore_closed_ && semaphore_ != nullptr &&
      semaphore_ != SEM_FAILED) {
    cleanup_posix_sem_close(semaphore_, sem_name_);
    semaphore_ = SEM_FAILED;
    semaphore_closed_ = true;
  }
  if (!unlink_on_destroy_) {
    return;
  }
  const QByteArray shm_name = posix_name_bytes(shm_name_);
  const QByteArray sem_name = posix_name_bytes(sem_name_);
  if (!shm_unlinked_) {
    shm_unlinked_ = cleanup_posix_unlink(QStringLiteral("shm_unlink"),
                                         shm_name, shm_name_, ::shm_unlink);
  }
  if (!sem_unlinked_) {
    sem_unlinked_ = cleanup_posix_unlink(QStringLiteral("sem_unlink"),
                                         sem_name, sem_name_, ::sem_unlink);
  }
}

std::shared_ptr<PosixTaskIpcMapping> PosixTaskIpcMapping::create_owner(
    const QByteArray& payload, const QString& owner_instance_id,
    TaskIpcCommandKind command, bool refresh_after_finish,
    quint64 session_id, quint32 generation, QString* error_message) {
  if (payload.isEmpty() || payload.size() > kTaskIpcPerTaskPayloadCapacity) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request payload exceeds per-task shared-memory capacity.");
    }
    return nullptr;
  }

  for (int attempt = 0; attempt < 8; ++attempt) {
    const QString suffix = short_posix_suffix();
    const QString shm_name = QStringLiteral("/z7-task-%1").arg(suffix);
    const QString sem_name = QStringLiteral("/z7-sem-%1").arg(suffix);
    const QByteArray shm_name_bytes = posix_name_bytes(shm_name);
    const QByteArray sem_name_bytes = posix_name_bytes(sem_name);

    int fd = ::shm_open(shm_name_bytes.constData(),
                        O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1 && errno == EEXIST) {
      continue;
    }
    if (fd == -1) {
      if (error_message != nullptr) {
        *error_message = posix_errno_message(QStringLiteral("shm_open"));
      }
      return nullptr;
    }

    const auto cleanup_shm = [&]() {
      cleanup_posix_close(fd, shm_name);
      fd = -1;
      cleanup_posix_unlink(QStringLiteral("shm_unlink"), shm_name_bytes,
                           shm_name, ::shm_unlink);
    };

    if (::ftruncate(fd, static_cast<off_t>(sizeof(TaskIpcPerTaskRaw))) == -1) {
      if (error_message != nullptr) {
        *error_message = posix_errno_message(QStringLiteral("ftruncate"));
      }
      cleanup_shm();
      return nullptr;
    }

    void* mapping =
        ::mmap(nullptr, sizeof(TaskIpcPerTaskRaw), PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
      if (error_message != nullptr) {
        *error_message = posix_errno_message(QStringLiteral("mmap"));
      }
      cleanup_shm();
      return nullptr;
    }

    sem_t* semaphore =
        ::sem_open(sem_name_bytes.constData(), O_CREAT | O_EXCL, 0600, 0);
    if (semaphore == SEM_FAILED && errno == EEXIST) {
      cleanup_posix_munmap(mapping, shm_name);
      cleanup_shm();
      continue;
    }
    if (semaphore == SEM_FAILED) {
      if (error_message != nullptr) {
        *error_message = posix_errno_message(QStringLiteral("sem_open"));
      }
      cleanup_posix_munmap(mapping, shm_name);
      cleanup_shm();
      return nullptr;
    }

    auto* raw = static_cast<TaskIpcPerTaskRaw*>(mapping);
    std::memset(raw, 0, sizeof(TaskIpcPerTaskRaw));
    raw->magic = kTaskIpcMagic;
    raw->version = kTaskIpcVersion;
    raw->slot.generation = generation;
    raw->slot.session_id = session_id;
    raw->slot.state = static_cast<quint32>(TaskIpcSlotState::kDispatched);
    raw->slot.command_kind = static_cast<quint32>(command);
    raw->slot.published_event_sequence = kTaskIpcDispatchedEventSequence;
    raw->slot.acknowledged_event_sequence = 0U;
    raw->slot.result_code = 0;
    raw->slot.refresh_after_finish = refresh_after_finish ? 1U : 0U;
    raw->slot.launcher_pid =
        static_cast<qint64>(QCoreApplication::applicationPid());
    raw->slot.worker_pid = 0;
    raw->slot.request_pool_slot = 0;
    raw->slot.request_payload_size = static_cast<quint32>(payload.size());
    raw->slot.updated_msecs = now_msecs();
    write_fixed_utf8(owner_instance_id, raw->slot.owner_instance_id,
                     kTaskIpcOwnerInstanceIdCapacity);
    std::memcpy(raw->payload, payload.constData(),
                static_cast<size_t>(payload.size()));

    return std::shared_ptr<PosixTaskIpcMapping>(new PosixTaskIpcMapping(
        shm_name, sem_name, fd, mapping, semaphore, owner_instance_id, true));
  }

  if (error_message != nullptr) {
    *error_message =
        QStringLiteral("shm_open failed: could not allocate unique task IPC names.");
  }
  return nullptr;
}

std::shared_ptr<PosixTaskIpcMapping> PosixTaskIpcMapping::open_worker(
    const QString& shm_name, const QString& sem_name, QString* error_message) {
  const QString trimmed_shm = shm_name.trimmed();
  const QString trimmed_sem = sem_name.trimmed();
  if (trimmed_shm.isEmpty() || trimmed_sem.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC worker requires shm and semaphore names.");
    }
    return nullptr;
  }

  const QByteArray shm_name_bytes = posix_name_bytes(trimmed_shm);
  const QByteArray sem_name_bytes = posix_name_bytes(trimmed_sem);
  sem_t* semaphore = ::sem_open(sem_name_bytes.constData(), 0);
  if (semaphore == SEM_FAILED) {
    if (error_message != nullptr) {
      *error_message = posix_errno_message(QStringLiteral("sem_open"));
    }
    return nullptr;
  }

  int fd = ::shm_open(shm_name_bytes.constData(), O_RDWR, 0);
  if (fd == -1) {
    if (error_message != nullptr) {
      *error_message = posix_errno_message(QStringLiteral("shm_open"));
    }
    cleanup_posix_sem_close(semaphore, trimmed_sem);
    return nullptr;
  }

  void* mapping =
      ::mmap(nullptr, sizeof(TaskIpcPerTaskRaw), PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    if (error_message != nullptr) {
      *error_message = posix_errno_message(QStringLiteral("mmap"));
    }
    cleanup_posix_close(fd, trimmed_shm);
    cleanup_posix_sem_close(semaphore, trimmed_sem);
    return nullptr;
  }

  auto* raw = static_cast<TaskIpcPerTaskRaw*>(mapping);
  if (raw->magic != kTaskIpcMagic || raw->version != kTaskIpcVersion ||
      raw->slot.request_payload_size == 0U ||
      raw->slot.request_payload_size >
          static_cast<quint32>(kTaskIpcPerTaskPayloadCapacity)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC per-task shared memory header is invalid.");
    }
    cleanup_posix_munmap(mapping, trimmed_shm);
    cleanup_posix_close(fd, trimmed_shm);
    cleanup_posix_sem_close(semaphore, trimmed_sem);
    return nullptr;
  }

  return std::shared_ptr<PosixTaskIpcMapping>(new PosixTaskIpcMapping(
      trimmed_shm, trimmed_sem, fd, mapping, semaphore, QString(), false));
}

TaskIpcPerTaskRaw* PosixTaskIpcMapping::raw() const {
  if (mapping_ == nullptr || mapping_ == MAP_FAILED) {
    return nullptr;
  }
  return static_cast<TaskIpcPerTaskRaw*>(mapping_);
}

sem_t* PosixTaskIpcMapping::semaphore() const { return semaphore_; }

QString PosixTaskIpcMapping::shm_name() const { return shm_name_; }

QString PosixTaskIpcMapping::sem_name() const { return sem_name_; }

bool PosixTaskIpcMapping::start_owner_waiter(QString* error_message) {
  if (!unlink_on_destroy_ || waiter_started_) {
    return true;
  }
  if (semaphore_ == nullptr || semaphore_ == SEM_FAILED) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC semaphore is unavailable.");
    }
    return false;
  }
  TaskIpcPerTaskRaw* raw = this->raw();
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC shared memory mapping is unavailable.");
    }
    return false;
  }

  stop_waiter_.store(false, std::memory_order_release);
  try {
    waiter_thread_ = std::thread([this]() { owner_wait_loop(); });
  } catch (const std::system_error& error) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to start task IPC semaphore waiter: %1")
                           .arg(QString::fromLocal8Bit(error.what()));
    }
    return false;
  }
  waiter_started_ = true;
  return true;
}

bool PosixTaskIpcMapping::start_unclaimed_timer(QString* error_message) {
  if (!unlink_on_destroy_) {
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(unclaimed_timer_source_mutex_);
    if (unclaimed_timer_source_ != nullptr) {
      return true;
    }
  }

  dispatch_source_t source = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
  if (source == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to create task IPC unclaimed timer.");
    }
    return false;
  }

  dispatch_set_context(source, this);
  dispatch_source_set_event_handler_f(
      source, &PosixTaskIpcMapping::unclaimed_timer_event_handler);
  dispatch_source_set_cancel_handler_f(
      source, &PosixTaskIpcMapping::unclaimed_timer_cancel_handler);
  dispatch_source_set_timer(
      source,
      dispatch_time(DISPATCH_TIME_NOW,
                    static_cast<int64_t>(kUnclaimedDispatchedReclaimMsecs) *
                        NSEC_PER_MSEC),
      DISPATCH_TIME_FOREVER, 0);
  {
    std::lock_guard<std::mutex> lock(unclaimed_timer_source_mutex_);
    unclaimed_timer_source_ = source;
    unclaimed_timer_source_cancel_complete_ = false;
  }
  dispatch_resume(source);
  return true;
}

bool PosixTaskIpcMapping::start_worker_exit_monitor(QString* error_message) {
  if (!unlink_on_destroy_) {
    return true;
  }
  TaskIpcPerTaskRaw* raw = this->raw();
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC shared memory mapping is unavailable.");
    }
    return false;
  }
  const qint64 worker_pid_value =
      std::atomic_ref<qint64>(raw->slot.worker_pid).load(std::memory_order_acquire);
  if (worker_pid_value <= 0) {
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(worker_exit_source_mutex_);
    if (worker_exit_source_ != nullptr) {
      return true;
    }
  }

  dispatch_source_t source = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_PROC,
      static_cast<uintptr_t>(static_cast<pid_t>(worker_pid_value)),
      DISPATCH_PROC_EXIT,
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
  if (source == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to create task IPC worker-exit monitor.");
    }
    return false;
  }

  dispatch_set_context(source, this);
  dispatch_source_set_event_handler_f(
      source, &PosixTaskIpcMapping::worker_exit_event_handler);
  dispatch_source_set_cancel_handler_f(
      source, &PosixTaskIpcMapping::worker_exit_cancel_handler);
  {
    std::lock_guard<std::mutex> lock(worker_exit_source_mutex_);
    worker_exit_source_ = source;
    worker_exit_source_cancel_complete_ = false;
  }
  dispatch_resume(source);

  if (!process_is_alive(worker_pid_value)) {
    handle_worker_exit_event();
  }
  return true;
}

void PosixTaskIpcMapping::stop_unclaimed_timer() {
  dispatch_source_t source = nullptr;
  {
    std::lock_guard<std::mutex> lock(unclaimed_timer_source_mutex_);
    source = unclaimed_timer_source_;
  }
  if (source == nullptr) {
    return;
  }

  dispatch_source_cancel(source);
  {
    std::unique_lock<std::mutex> lock(unclaimed_timer_source_mutex_);
    unclaimed_timer_source_cv_.wait(lock, [this]() {
      return unclaimed_timer_source_cancel_complete_;
    });
    if (unclaimed_timer_source_ == source) {
      unclaimed_timer_source_ = nullptr;
    }
    unclaimed_timer_source_cancel_complete_ = false;
  }
  dispatch_release(source);
}

void PosixTaskIpcMapping::stop_worker_exit_monitor() {
  dispatch_source_t source = nullptr;
  {
    std::lock_guard<std::mutex> lock(worker_exit_source_mutex_);
    source = worker_exit_source_;
  }
  if (source == nullptr) {
    return;
  }

  dispatch_source_cancel(source);
  {
    std::unique_lock<std::mutex> lock(worker_exit_source_mutex_);
    worker_exit_source_cv_.wait(lock, [this]() {
      return worker_exit_source_cancel_complete_;
    });
    if (worker_exit_source_ == source) {
      worker_exit_source_ = nullptr;
    }
    worker_exit_source_cancel_complete_ = false;
  }
  dispatch_release(source);
}

void PosixTaskIpcMapping::stop_owner_waiter() {
  if (!waiter_started_) {
    return;
  }

  stop_waiter_.store(true, std::memory_order_release);
  stop_unclaimed_timer();
  stop_worker_exit_monitor();
  if (semaphore_ != nullptr && semaphore_ != SEM_FAILED) {
    ::sem_post(semaphore_);
  }
  if (waiter_thread_.joinable()) {
    waiter_thread_.join();
  }
  waiter_started_ = false;
}

void PosixTaskIpcMapping::enable_owner_notification_delivery() {
  bool should_enqueue = false;
  {
    std::lock_guard<std::mutex> lock(owner_notification_mutex_);
    owner_notification_delivery_enabled_ = true;
    if (pending_owner_notification_) {
      pending_owner_notification_ = false;
      should_enqueue = true;
    }
  }
  if (should_enqueue) {
    posix_event_dispatcher().enqueue(owner_instance_id_);
  }
}

void PosixTaskIpcMapping::notify_owner_or_defer() {
  bool should_enqueue = false;
  {
    std::lock_guard<std::mutex> lock(owner_notification_mutex_);
    if (owner_notification_delivery_enabled_) {
      should_enqueue = true;
    } else {
      pending_owner_notification_ = true;
    }
  }
  if (should_enqueue) {
    posix_event_dispatcher().enqueue(owner_instance_id_);
  }
}

void PosixTaskIpcMapping::unclaimed_timer_event_handler(void* context) {
  auto* mapping = static_cast<PosixTaskIpcMapping*>(context);
  if (mapping != nullptr) {
    mapping->handle_unclaimed_timer_event();
  }
}

void PosixTaskIpcMapping::unclaimed_timer_cancel_handler(void* context) {
  auto* mapping = static_cast<PosixTaskIpcMapping*>(context);
  if (mapping != nullptr) {
    mapping->handle_unclaimed_timer_cancel();
  }
}

void PosixTaskIpcMapping::worker_exit_event_handler(void* context) {
  auto* mapping = static_cast<PosixTaskIpcMapping*>(context);
  if (mapping != nullptr) {
    mapping->handle_worker_exit_event();
  }
}

void PosixTaskIpcMapping::worker_exit_cancel_handler(void* context) {
  auto* mapping = static_cast<PosixTaskIpcMapping*>(context);
  if (mapping != nullptr) {
    mapping->handle_worker_exit_cancel();
  }
}

void PosixTaskIpcMapping::handle_unclaimed_timer_event() {
  bool should_enqueue = false;
  TaskIpcPerTaskRaw* raw = this->raw();
  if (raw != nullptr) {
    SharedMemoryLock lock(&raw->lock,
                          task_ipc_per_task_lock_wake_key(shm_name_));
    if (lock.ok()) {
      should_enqueue =
          publish_task_ipc_unclaimed_timeout_completion(&raw->slot, now_msecs());
    }
  }
  if (should_enqueue) {
    post_posix_task_notification(this, nullptr);
  }

  dispatch_source_t source = nullptr;
  {
    std::lock_guard<std::mutex> lock(unclaimed_timer_source_mutex_);
    source = unclaimed_timer_source_;
  }
  if (source != nullptr) {
    dispatch_source_cancel(source);
  }
}

void PosixTaskIpcMapping::handle_unclaimed_timer_cancel() {
  {
    std::lock_guard<std::mutex> lock(unclaimed_timer_source_mutex_);
    unclaimed_timer_source_cancel_complete_ = true;
  }
  unclaimed_timer_source_cv_.notify_all();
}

void PosixTaskIpcMapping::handle_worker_exit_event() {
  bool should_enqueue = false;
  TaskIpcPerTaskRaw* raw = this->raw();
  if (raw != nullptr) {
    SharedMemoryLock lock(&raw->lock,
                          task_ipc_per_task_lock_wake_key(shm_name_));
    if (lock.ok()) {
      should_enqueue =
          publish_task_ipc_worker_exit_completion(&raw->slot, now_msecs());
    }
  }
  if (should_enqueue) {
    post_posix_task_notification(this, nullptr);
  }

  dispatch_source_t source = nullptr;
  {
    std::lock_guard<std::mutex> lock(worker_exit_source_mutex_);
    source = worker_exit_source_;
  }
  if (source != nullptr) {
    dispatch_source_cancel(source);
  }
}

void PosixTaskIpcMapping::handle_worker_exit_cancel() {
  {
    std::lock_guard<std::mutex> lock(worker_exit_source_mutex_);
    worker_exit_source_cancel_complete_ = true;
  }
  worker_exit_source_cv_.notify_all();
}

void PosixTaskIpcMapping::owner_wait_loop() {
  while (!stop_waiter_.load(std::memory_order_acquire)) {
    if (::sem_wait(semaphore_) == -1) {
      if (errno == EINTR) {
        continue;
      }
      notify_owner_or_defer();
      return;
    }

    if (stop_waiter_.load(std::memory_order_acquire)) {
      return;
    }
    notify_owner_or_defer();
  }
}

quint64 next_posix_task_session_id() {
  static std::atomic<quint64> next_session_id{1};
  quint64 value = next_session_id.fetch_add(1, std::memory_order_relaxed);
  if (value == 0) {
    value = next_session_id.fetch_add(1, std::memory_order_relaxed);
  }
  return value;
}

void register_posix_task_mapping(
    const std::shared_ptr<PosixTaskIpcMapping>& mapping) {
  if (mapping == nullptr || mapping->raw() == nullptr) {
    return;
  }
  static_cast<void>(posix_event_dispatcher());
  std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
  posix_task_registry().push_back(mapping);
}

std::shared_ptr<PosixTaskIpcMapping> find_posix_task_mapping(
    quint64 session_id, quint32 generation) {
  std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
  for (const std::shared_ptr<PosixTaskIpcMapping>& mapping :
       posix_task_registry()) {
    const TaskIpcPerTaskRaw* raw = mapping == nullptr ? nullptr : mapping->raw();
    if (raw != nullptr && raw->slot.session_id == session_id &&
        raw->slot.generation == generation) {
      return mapping;
    }
  }
  return nullptr;
}

QVector<std::shared_ptr<PosixTaskIpcMapping>> posix_task_mappings_snapshot() {
  std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
  return posix_task_registry();
}

void remove_posix_task_mapping(
    const std::shared_ptr<PosixTaskIpcMapping>& mapping) {
  if (mapping == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
  QVector<std::shared_ptr<PosixTaskIpcMapping>>& registry =
      posix_task_registry();
  for (auto it = registry.begin(); it != registry.end(); ++it) {
    if ((*it).get() == mapping.get()) {
      registry.erase(it);
      return;
    }
  }
}

void set_posix_worker_endpoint(const QString& shm_name,
                               const QString& sem_name) {
  std::lock_guard<std::mutex> lock(posix_worker_endpoint_mutex());
  posix_worker_shm_name_storage() = shm_name;
  posix_worker_sem_name_storage() = sem_name;
}

QString posix_worker_shm_name() {
  std::lock_guard<std::mutex> lock(posix_worker_endpoint_mutex());
  return posix_worker_shm_name_storage();
}

QString posix_worker_sem_name() {
  std::lock_guard<std::mutex> lock(posix_worker_endpoint_mutex());
  return posix_worker_sem_name_storage();
}

bool post_posix_task_notification(PosixTaskIpcMapping* mapping,
                                  QString* error_message) {
  if (mapping == nullptr || mapping->semaphore() == nullptr ||
      mapping->semaphore() == SEM_FAILED) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC semaphore is unavailable.");
    }
    return false;
  }
  if (::sem_post(mapping->semaphore()) == -1) {
    if (error_message != nullptr) {
      *error_message = posix_errno_message(QStringLiteral("sem_post"));
    }
    return false;
  }
  return true;
}

void set_posix_event_notifier(const QString& owner_instance_id,
                              TaskIpcEventNotifier notifier) {
  const QString trimmed_owner = owner_instance_id.trimmed();
  if (trimmed_owner.isEmpty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(posix_event_notifier_mutex());
  if (notifier) {
    posix_event_notifiers().insert(trimmed_owner, std::move(notifier));
  } else {
    posix_event_notifiers().remove(trimmed_owner);
  }
}

void clear_posix_event_notifier(const QString& owner_instance_id) {
  const QString trimmed_owner = owner_instance_id.trimmed();
  if (trimmed_owner.isEmpty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(posix_event_notifier_mutex());
  posix_event_notifiers().remove(trimmed_owner);
}

TaskIpcEventNotifier posix_event_notifier_for_owner(
    const QString& owner_instance_id) {
  const QString trimmed_owner = owner_instance_id.trimmed();
  if (trimmed_owner.isEmpty()) {
    return TaskIpcEventNotifier();
  }

  std::lock_guard<std::mutex> lock(posix_event_notifier_mutex());
  return posix_event_notifiers().value(trimmed_owner);
}

}  // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
