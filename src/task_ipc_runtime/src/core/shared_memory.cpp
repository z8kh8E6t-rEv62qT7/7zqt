#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QSharedMemory>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>

#include "internal.h"

#ifndef Q_OS_WIN
#include <cerrno>
#include <csignal>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace z7::task_ipc_runtime::task_ipc_internal {
    namespace {

        std::mutex& bootstrap_memory_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::shared_ptr<QSharedMemory>& bootstrap_memory_lease() {
            static std::shared_ptr<QSharedMemory> memory;
            return memory;
        }

        std::mutex& request_pool_memory_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::shared_ptr<QSharedMemory>& request_pool_memory_lease() {
            static std::shared_ptr<QSharedMemory> memory;
            return memory;
        }

        QString hashed_task_ipc_key(QString const& prefix, QString const& value) {
            QByteArray const hash = QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex();
            return QStringLiteral("%1%2").arg(prefix,
                                              QString::fromLatin1(hash.constData(), std::min<int>(hash.size(), 24)));
        }

        quint32 current_process_id() {
            qint64 const pid = static_cast<qint64>(QCoreApplication::applicationPid());
            if (pid <= 0 || pid > std::numeric_limits<quint32>::max()) {
                return 0;
            }
            return static_cast<quint32>(pid);
        }

        class LockWaiterEntry final : public std::enable_shared_from_this<LockWaiterEntry> {
        public:
            explicit LockWaiterEntry(QString wake_key) : wake_key_(std::move(wake_key)), semaphore_(QString()) {}

            bool start(QString* error_message) {
                if (running_.load(std::memory_order_acquire)) {
                    return true;
                }

                semaphore_.setNativeKey(QSystemSemaphore::platformSafeKey(wake_key_), 0, QSystemSemaphore::Open);
                if (semaphore_.error() != QSystemSemaphore::NoError) {
                    if (error_message != nullptr) {
                        *error_message = semaphore_.errorString().trimmed().isEmpty()
                                           ? QStringLiteral("Failed to initialize task IPC lock waiter semaphore.")
                                           : semaphore_.errorString().trimmed();
                    }
                    return false;
                }

                try {
                    std::shared_ptr<LockWaiterEntry> const self = shared_from_this();
                    std::thread([self]() { self->wait_loop(); }).detach();
                } catch (std::system_error const& error) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start task IPC lock waiter thread: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return false;
                }

                running_.store(true, std::memory_order_release);
                return true;
            }

            quint64 generation() const {
                std::lock_guard<std::mutex> lock(mutex_);
                return generation_;
            }

            bool wait_for_generation_change(quint64 observed_generation, qint64 deadline_msecs) {
                std::unique_lock<std::mutex> lock(mutex_);
                while (generation_ == observed_generation) {
                    qint64 const remaining_msecs = deadline_msecs - now_msecs();
                    if (remaining_msecs <= 0) {
                        return false;
                    }
                    cv_.wait_for(lock, std::chrono::milliseconds(remaining_msecs), [this, observed_generation]() {
                        return generation_ != observed_generation;
                    });
                }
                return true;
            }

        private:
            void wait_loop() {
                for (;;) {
                    if (!semaphore_.acquire()) {
                        running_.store(false, std::memory_order_release);
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        ++generation_;
                    }
                    cv_.notify_all();
                }
            }

            QString wake_key_;
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            quint64 generation_ = 0;
            QSystemSemaphore semaphore_;
            std::atomic_bool running_{false};
        };

        std::mutex& lock_waiter_registry_mutex() {
            static std::mutex* mutex = new std::mutex;
            return *mutex;
        }

        QHash<QString, std::shared_ptr<LockWaiterEntry>>& lock_waiter_registry() {
            static QHash<QString, std::shared_ptr<LockWaiterEntry>>* registry =
                new QHash<QString, std::shared_ptr<LockWaiterEntry>>();
            return *registry;
        }

        std::shared_ptr<LockWaiterEntry> ensure_lock_waiter_entry(QString const& wake_key, QString* error_message) {
            QString const trimmed_key = wake_key.trimmed();
            if (trimmed_key.isEmpty()) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Task IPC lock wake key is empty.");
                }
                return nullptr;
            }

            std::lock_guard<std::mutex> lock(lock_waiter_registry_mutex());
            QHash<QString, std::shared_ptr<LockWaiterEntry>>& registry = lock_waiter_registry();
            std::shared_ptr<LockWaiterEntry>& entry = registry[trimmed_key];
            if (entry == nullptr) {
                entry = std::make_shared<LockWaiterEntry>(trimmed_key);
            }
            if (!entry->start(error_message)) {
                entry.reset();
                registry.remove(trimmed_key);
                return nullptr;
            }
            return entry;
        }

        void notify_task_ipc_lock_waiters(QString const& wake_key) {
            QString const trimmed_key = wake_key.trimmed();
            if (trimmed_key.isEmpty()) {
                return;
            }

            QSystemSemaphore semaphore(QSystemSemaphore::platformSafeKey(trimmed_key), 0, QSystemSemaphore::Open);
            if (semaphore.error() != QSystemSemaphore::NoError) {
                return;
            }
            semaphore.release();
        }

        bool try_acquire_lock(RobustLockRaw* lock, bool* out_busy, QString* out_error) {
            if (out_busy != nullptr) {
                *out_busy = false;
            }
            if (out_error != nullptr) {
                out_error->clear();
            }
            if (lock == nullptr) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Task IPC lock pointer is null.");
                }
                return false;
            }

            quint32 const self_pid = current_process_id();
            if (self_pid == 0U) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Current process PID is unavailable.");
                }
                return false;
            }

            std::atomic_ref<quint32> owner(lock->locker_pid);
            quint32 expected = 0U;
            if (owner.compare_exchange_strong(
                    expected, self_pid, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return true;
            }

            quint32 const current_owner = expected;
            if (current_owner == self_pid) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Task IPC lock is already held by current process.");
                }
                return false;
            }

            if (current_owner != 0U && !process_is_alive(static_cast<qint64>(current_owner))) {
                quint32 stale_owner = current_owner;
                if (owner.compare_exchange_strong(
                        stale_owner, 0U, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    expected = 0U;
                    if (owner.compare_exchange_strong(
                            expected, self_pid, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        return true;
                    }
                }
            }

            if (out_busy != nullptr) {
                *out_busy = true;
            }
            return false;
        }

        bool release_lock(RobustLockRaw* lock, QString* out_error) {
            if (out_error != nullptr) {
                out_error->clear();
            }
            if (lock == nullptr) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Task IPC lock pointer is null.");
                }
                return false;
            }

            quint32 const self_pid = current_process_id();
            if (self_pid == 0U) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Current process PID is unavailable.");
                }
                return false;
            }

            std::atomic_ref<quint32> owner(lock->locker_pid);
            quint32 const current_owner = owner.load(std::memory_order_acquire);
            if (current_owner != self_pid) {
                if (out_error != nullptr) {
                    *out_error = QStringLiteral("Task IPC lock is not owned by current process.");
                }
                return false;
            }

            owner.store(0U, std::memory_order_release);
            return true;
        }

    } // namespace

    SharedMemoryLock::SharedMemoryLock(RobustLockRaw* lock, QString wake_key) :
        lock_(lock), wake_key_(std::move(wake_key)) {
        if (!wake_key_.trimmed().isEmpty()) {
            QString waiter_error;
            if (ensure_lock_waiter_entry(wake_key_, &waiter_error) == nullptr) {
                error_ = waiter_error;
                return;
            }
        }
        locked_ = try_acquire_lock(lock_, &busy_, &error_);
    }

    SharedMemoryLock::~SharedMemoryLock() {
        if (locked_) {
            QString ignored_error;
            if (release_lock(lock_, &ignored_error) && !wake_key_.trimmed().isEmpty()) {
                notify_task_ipc_lock_waiters(wake_key_);
            }
        }
    }

    bool SharedMemoryLock::ok() const {
        return locked_;
    }

    bool SharedMemoryLock::busy() const {
        return busy_;
    }

    QString SharedMemoryLock::error() const {
        return error_;
    }

    std::shared_ptr<QSharedMemory> current_bootstrap_memory_lease() {
        std::lock_guard<std::mutex> lock(bootstrap_memory_mutex());
        return bootstrap_memory_lease();
    }

    void update_bootstrap_memory_lease(std::shared_ptr<QSharedMemory> const& memory) {
        std::lock_guard<std::mutex> lock(bootstrap_memory_mutex());
        bootstrap_memory_lease() = memory;
    }

    std::shared_ptr<QSharedMemory> current_request_pool_memory_lease() {
        std::lock_guard<std::mutex> lock(request_pool_memory_mutex());
        return request_pool_memory_lease();
    }

    void update_request_pool_memory_lease(std::shared_ptr<QSharedMemory> const& memory) {
        std::lock_guard<std::mutex> lock(request_pool_memory_mutex());
        request_pool_memory_lease() = memory;
    }

    qint64 now_msecs() {
        return QDateTime::currentMSecsSinceEpoch();
    }

    bool process_is_alive(qint64 pid) {
        if (pid <= 0) {
            return false;
        }
#ifdef Q_OS_WIN
        HANDLE handle = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (handle == nullptr) {
            return false;
        }
        const DWORD wait_result = ::WaitForSingleObject(handle, 0);
        ::CloseHandle(handle);
        return wait_result == WAIT_TIMEOUT;
#else
        int const rc = ::kill(static_cast<pid_t>(pid), 0);
        if (rc == 0) {
            return true;
        }
        return errno == EPERM;
#endif
    }

    QString error_string_from_shared_memory(QSharedMemory* memory, QString const& fallback) {
        if (memory == nullptr) {
            return fallback;
        }
        QString const err = memory->errorString().trimmed();
        if (!err.isEmpty()) {
            return err;
        }
        return fallback;
    }

    QString task_ipc_bootstrap_lock_wake_key() {
        QString const bootstrap_key = z7::task_ipc_runtime::task_ipc_bootstrap_key();
        if (bootstrap_key.trimmed().isEmpty()) {
            return QString();
        }
        return hashed_task_ipc_key(QStringLiteral("z7.taskipc.lock.bootstrap."), bootstrap_key);
    }

    QString task_ipc_per_task_lock_wake_key(QString const& shm_name) {
        QString const trimmed_shm_name = shm_name.trimmed();
        if (trimmed_shm_name.isEmpty()) {
            return QString();
        }
        return hashed_task_ipc_key(QStringLiteral("z7.taskipc.lock.task."), trimmed_shm_name);
    }

    std::unique_ptr<SharedMemoryLock> wait_for_shared_memory_lock(RobustLockRaw* lock,
                                                                  QString const& wake_key,
                                                                  int timeout_msecs,
                                                                  QString const& busy_message,
                                                                  QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (lock == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC lock pointer is null.");
            }
            return nullptr;
        }

        std::shared_ptr<LockWaiterEntry> waiter_entry = ensure_lock_waiter_entry(wake_key, error_message);
        if (waiter_entry == nullptr) {
            return nullptr;
        }

        qint64 const deadline = timeout_msecs <= 0 ? now_msecs() : now_msecs() + timeout_msecs;
        quint64 observed_generation = waiter_entry->generation();
        for (;;) {
            auto candidate = std::make_unique<SharedMemoryLock>(lock, wake_key);
            if (candidate->ok()) {
                return candidate;
            }
            if (!candidate->busy()) {
                if (error_message != nullptr) {
                    *error_message = candidate->error();
                }
                return nullptr;
            }
            quint64 const current_generation = waiter_entry->generation();
            if (current_generation != observed_generation) {
                observed_generation = current_generation;
                continue;
            }
            if (now_msecs() >= deadline) {
                if (error_message != nullptr) {
                    *error_message = busy_message.trimmed().isEmpty() ? QStringLiteral("Task IPC lock remained busy.")
                                                                      : busy_message;
                }
                return nullptr;
            }
            if (!waiter_entry->wait_for_generation_change(observed_generation, deadline)) {
                if (error_message != nullptr) {
                    *error_message = busy_message.trimmed().isEmpty() ? QStringLiteral("Task IPC lock remained busy.")
                                                                      : busy_message;
                }
                return nullptr;
            }
            observed_generation = waiter_entry->generation();
        }
    }

    QString read_fixed_utf8(char const* bytes, int capacity) {
        if (bytes == nullptr || capacity <= 0) {
            return QString();
        }
        int len = 0;
        while (len < capacity && bytes[len] != '\0') {
            ++len;
        }
        if (len <= 0) {
            return QString();
        }
        return QString::fromUtf8(bytes, len);
    }

    bool validate_task_ipc_owner_instance_id(QString const& owner_instance_id,
                                             QString* out_normalized_owner,
                                             QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (out_normalized_owner != nullptr) {
            out_normalized_owner->clear();
        }

        QString const normalized_owner = owner_instance_id.trimmed();
        if (normalized_owner.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC owner instance id is empty.");
            }
            return false;
        }

        QByteArray const utf8 = normalized_owner.toUtf8();
        if (utf8.size() > kTaskIpcOwnerInstanceIdMaxUtf8Bytes) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC owner instance id exceeds the maximum UTF-8 length "
                                                "(actual=%1 max=%2).")
                                     .arg(utf8.size())
                                     .arg(kTaskIpcOwnerInstanceIdMaxUtf8Bytes);
            }
            return false;
        }

        if (out_normalized_owner != nullptr) {
            *out_normalized_owner = normalized_owner;
        }
        return true;
    }

    void write_fixed_utf8(QString const& value, char* out, int capacity) {
        if (out == nullptr || capacity <= 0) {
            return;
        }
        std::memset(out, 0, static_cast<size_t>(capacity));
        if (value.isEmpty()) {
            return;
        }
        QByteArray const utf8 = value.toUtf8();
        int const utf8_size = static_cast<int>(utf8.size());
        int copy_len = std::min(capacity - 1, utf8_size);
        if (copy_len < utf8_size) {
            while (copy_len > 0
                   && (static_cast<unsigned char>(utf8.at(copy_len)) & 0xC0U) == 0x80U) {
                --copy_len;
            }
        }
        if (copy_len <= 0) {
            return;
        }
        std::memcpy(out, utf8.constData(), static_cast<size_t>(copy_len));
        out[copy_len] = '\0';
    }

} // namespace z7::task_ipc_runtime::task_ipc_internal
