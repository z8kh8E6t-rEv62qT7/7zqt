#include "internal.h"

#if Z7_TASK_IPC_PER_TASK_POSIX

#include <QCoreApplication>
#include <QHash>
#include <QUuid>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

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

        QString posix_errno_message(QString const& operation, int error_number = errno) {
            return QStringLiteral("%1 failed: %2").arg(operation, QString::fromLocal8Bit(std::strerror(error_number)));
        }

        QString short_posix_suffix() {
            QString const uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
            qint64 const pid = QCoreApplication::applicationPid();
            return QStringLiteral("%1-%2").arg(pid % 100000).arg(uuid);
        }

        QByteArray posix_name_bytes(QString const& name) {
            return name.toUtf8();
        }

        bool valid_posix_name(QString const& name) {
            QByteArray const encoded = posix_name_bytes(name.trimmed());
            if (encoded.size() < 2 || encoded.size() >= kTaskIpcPosixNameCapacity || encoded.front() != '/') {
                return false;
            }
            return encoded.indexOf('/', 1) == -1 && encoded.indexOf('\0') == -1;
        }

        bool cleanup_posix_unlink(QByteArray const& name, int (*unlink_fn)(char const*)) {
            if (name.isEmpty()) {
                return true;
            }
            if (unlink_fn(name.constData()) == 0) {
                return true;
            }
            return errno == ENOENT;
        }

        void cleanup_posix_munmap(void* mapping) {
            if (mapping != nullptr && mapping != MAP_FAILED) {
                ::munmap(mapping, sizeof(TaskIpcPerTaskRaw));
            }
        }

        void cleanup_posix_close(int fd) {
            if (fd != -1) {
                ::close(fd);
            }
        }

        void cleanup_posix_sem_close(sem_t* semaphore) {
            if (semaphore != nullptr && semaphore != SEM_FAILED) {
                ::sem_close(semaphore);
            }
        }

        bool mapped_file_is_large_enough(int fd, QString* error_message) {
            struct stat status{};
            if (::fstat(fd, &status) == -1) {
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("fstat"));
                }
                return false;
            }
            if (status.st_size < static_cast<off_t>(sizeof(TaskIpcPerTaskRaw))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Task IPC per-task shared memory is truncated.");
                }
                return false;
            }
            return true;
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

            PosixEventDispatcher(PosixEventDispatcher const&) = delete;
            PosixEventDispatcher& operator=(PosixEventDispatcher const&) = delete;

            void enqueue(QString const& owner_instance_id) {
                QString const trimmed_owner = owner_instance_id.trimmed();
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
                        } catch (std::system_error const&) {
                            return;
                        }
                    }
                    try {
                        pending_.push_back(trimmed_owner);
                    } catch (std::exception const&) {
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
                        condition_.wait(lock, [this]() { return stopping_ || !pending_.empty(); });
                        if (stopping_ && pending_.empty()) {
                            return;
                        }
                        owner_instance_id = pending_.front();
                        pending_.pop_front();
                    }

                    TaskIpcEventNotifier notifier = posix_event_notifier_for_owner(owner_instance_id);
                    if (notifier) {
                        try {
                            notifier(owner_instance_id);
                        } catch (...) {}
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

    } // namespace

    PosixTaskIpcMapping::PosixTaskIpcMapping(QString shm_name,
                                             QString sem_name,
                                             QString cancel_sem_name,
                                             int fd,
                                             void* mapping,
                                             sem_t* event_semaphore,
                                             sem_t* cancel_semaphore,
                                             QString owner_instance_id,
                                             bool unlink_on_destroy) :
        shm_name_(std::move(shm_name)),
        sem_name_(std::move(sem_name)),
        cancel_sem_name_(std::move(cancel_sem_name)),
        owner_instance_id_(std::move(owner_instance_id)),
        fd_(fd),
        mapping_(mapping),
        event_semaphore_(event_semaphore),
        cancel_semaphore_(cancel_semaphore),
        unlink_on_destroy_(unlink_on_destroy),
        platform_monitor_(unlink_on_destroy ? create_posix_task_ipc_platform_monitor() : nullptr) {}

    PosixTaskIpcMapping::~PosixTaskIpcMapping() {
        stop_owner_waiter();
        stop_unclaimed_timer();
        stop_worker_exit_monitor();
        unlink_owned_names();

        cleanup_posix_sem_close(cancel_semaphore_);
        cancel_semaphore_ = SEM_FAILED;
        cleanup_posix_sem_close(event_semaphore_);
        event_semaphore_ = SEM_FAILED;
        cleanup_posix_munmap(mapping_);
        mapping_ = nullptr;
        cleanup_posix_close(fd_);
        fd_ = -1;
    }

    std::shared_ptr<PosixTaskIpcMapping> PosixTaskIpcMapping::create_owner(QByteArray const& payload,
                                                                           QString const& owner_instance_id,
                                                                           TaskIpcCommandKind command,
                                                                           bool refresh_after_finish,
                                                                           quint64 session_id,
                                                                           quint32 generation,
                                                                           QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (payload.isEmpty() || payload.size() > kTaskIpcPerTaskPayloadCapacity) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC request payload exceeds per-task shared-memory capacity.");
            }
            return nullptr;
        }

        for (int attempt = 0; attempt < 8; ++attempt) {
            QString const suffix = short_posix_suffix();
            QString const shm_name = QStringLiteral("/z7-task-%1").arg(suffix);
            QString const sem_name = QStringLiteral("/z7-sem-%1").arg(suffix);
            QString const cancel_sem_name = QStringLiteral("/z7-cancel-%1").arg(suffix);
            QByteArray const shm_name_bytes = posix_name_bytes(shm_name);
            QByteArray const sem_name_bytes = posix_name_bytes(sem_name);
            QByteArray const cancel_sem_name_bytes = posix_name_bytes(cancel_sem_name);

            int fd = ::shm_open(shm_name_bytes.constData(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd == -1 && errno == EEXIST) {
                continue;
            }
            if (fd == -1) {
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("shm_open"));
                }
                return nullptr;
            }

            void* mapping = MAP_FAILED;
            sem_t* event_semaphore = SEM_FAILED;
            sem_t* cancel_semaphore = SEM_FAILED;
            auto const cleanup_attempt = [&]() {
                cleanup_posix_sem_close(cancel_semaphore);
                cleanup_posix_sem_close(event_semaphore);
                cleanup_posix_munmap(mapping);
                cleanup_posix_close(fd);
                cleanup_posix_unlink(cancel_sem_name_bytes, ::sem_unlink);
                cleanup_posix_unlink(sem_name_bytes, ::sem_unlink);
                cleanup_posix_unlink(shm_name_bytes, ::shm_unlink);
            };

            if (::ftruncate(fd, static_cast<off_t>(sizeof(TaskIpcPerTaskRaw))) == -1) {
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("ftruncate"));
                }
                cleanup_attempt();
                return nullptr;
            }

            mapping = ::mmap(nullptr, sizeof(TaskIpcPerTaskRaw), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapping == MAP_FAILED) {
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("mmap"));
                }
                cleanup_attempt();
                return nullptr;
            }

            event_semaphore = ::sem_open(sem_name_bytes.constData(), O_CREAT | O_EXCL, 0600, 0);
            if (event_semaphore == SEM_FAILED) {
                int const error_number = errno;
                cleanup_attempt();
                if (error_number == EEXIST) {
                    continue;
                }
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("sem_open(event)"), error_number);
                }
                return nullptr;
            }

            cancel_semaphore = ::sem_open(cancel_sem_name_bytes.constData(), O_CREAT | O_EXCL, 0600, 0);
            if (cancel_semaphore == SEM_FAILED) {
                int const error_number = errno;
                cleanup_attempt();
                if (error_number == EEXIST) {
                    continue;
                }
                if (error_message != nullptr) {
                    *error_message = posix_errno_message(QStringLiteral("sem_open(cancel)"), error_number);
                }
                return nullptr;
            }

            auto* raw = static_cast<TaskIpcPerTaskRaw*>(mapping);
            std::memset(raw, 0, sizeof(TaskIpcPerTaskRaw));
            raw->magic = kTaskIpcMagic;
            raw->version = kTaskIpcPerTaskVersion;
            write_fixed_utf8(cancel_sem_name, raw->cancel_semaphore_name, kTaskIpcPosixNameCapacity);
            raw->slot.generation = generation;
            raw->slot.session_id = session_id;
            raw->slot.state = static_cast<quint32>(TaskIpcSlotState::kDispatched);
            raw->slot.command_kind = static_cast<quint32>(command);
            raw->slot.published_event_sequence = kTaskIpcDispatchedEventSequence;
            raw->slot.acknowledged_event_sequence = 0U;
            raw->slot.result_code = 0;
            raw->slot.refresh_after_finish = refresh_after_finish ? 1U : 0U;
            raw->slot.launcher_pid = static_cast<qint64>(QCoreApplication::applicationPid());
            raw->slot.worker_pid = 0;
            raw->slot.request_pool_slot = 0;
            raw->slot.request_payload_size = static_cast<quint32>(payload.size());
            raw->slot.updated_msecs = now_msecs();
            write_fixed_utf8(owner_instance_id, raw->slot.owner_instance_id, kTaskIpcOwnerInstanceIdCapacity);
            std::memcpy(raw->payload, payload.constData(), static_cast<size_t>(payload.size()));

            return std::shared_ptr<PosixTaskIpcMapping>(new PosixTaskIpcMapping(shm_name,
                                                                                sem_name,
                                                                                cancel_sem_name,
                                                                                fd,
                                                                                mapping,
                                                                                event_semaphore,
                                                                                cancel_semaphore,
                                                                                owner_instance_id,
                                                                                true));
        }

        if (error_message != nullptr) {
            *error_message = QStringLiteral("shm_open failed: could not allocate unique task IPC names.");
        }
        return nullptr;
    }

    std::shared_ptr<PosixTaskIpcMapping>
    PosixTaskIpcMapping::open_worker(QString const& shm_name, QString const& sem_name, QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        QString const trimmed_shm = shm_name.trimmed();
        QString const trimmed_sem = sem_name.trimmed();
        if (!valid_posix_name(trimmed_shm) || !valid_posix_name(trimmed_sem)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC worker requires valid shm and semaphore names.");
            }
            return nullptr;
        }

        QByteArray const shm_name_bytes = posix_name_bytes(trimmed_shm);
        int fd = ::shm_open(shm_name_bytes.constData(), O_RDWR, 0);
        if (fd == -1) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("shm_open"));
            }
            return nullptr;
        }
        if (!mapped_file_is_large_enough(fd, error_message)) {
            cleanup_posix_close(fd);
            return nullptr;
        }

        void* mapping = ::mmap(nullptr, sizeof(TaskIpcPerTaskRaw), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("mmap"));
            }
            cleanup_posix_close(fd);
            return nullptr;
        }

        auto* raw = static_cast<TaskIpcPerTaskRaw*>(mapping);
        bool const cancel_name_terminated =
            std::memchr(raw->cancel_semaphore_name, '\0', kTaskIpcPosixNameCapacity) != nullptr;
        QString const cancel_sem_name =
            cancel_name_terminated ? read_fixed_utf8(raw->cancel_semaphore_name, kTaskIpcPosixNameCapacity) : QString();
        if (raw->magic != kTaskIpcMagic
            || raw->version != kTaskIpcPerTaskVersion
            || raw->slot.request_payload_size == 0U
            || raw->slot.request_payload_size > static_cast<quint32>(kTaskIpcPerTaskPayloadCapacity)
            || !valid_posix_name(cancel_sem_name)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC per-task shared memory header is invalid.");
            }
            cleanup_posix_munmap(mapping);
            cleanup_posix_close(fd);
            return nullptr;
        }

        QByteArray const sem_name_bytes = posix_name_bytes(trimmed_sem);
        sem_t* event_semaphore = ::sem_open(sem_name_bytes.constData(), 0);
        if (event_semaphore == SEM_FAILED) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("sem_open(event)"));
            }
            cleanup_posix_munmap(mapping);
            cleanup_posix_close(fd);
            return nullptr;
        }

        QByteArray const cancel_sem_name_bytes = posix_name_bytes(cancel_sem_name);
        sem_t* cancel_semaphore = ::sem_open(cancel_sem_name_bytes.constData(), 0);
        if (cancel_semaphore == SEM_FAILED) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("sem_open(cancel)"));
            }
            cleanup_posix_sem_close(event_semaphore);
            cleanup_posix_munmap(mapping);
            cleanup_posix_close(fd);
            return nullptr;
        }

        return std::shared_ptr<PosixTaskIpcMapping>(new PosixTaskIpcMapping(trimmed_shm,
                                                                            trimmed_sem,
                                                                            cancel_sem_name,
                                                                            fd,
                                                                            mapping,
                                                                            event_semaphore,
                                                                            cancel_semaphore,
                                                                            QString(),
                                                                            false));
    }

    TaskIpcPerTaskRaw* PosixTaskIpcMapping::raw() const {
        if (mapping_ == nullptr || mapping_ == MAP_FAILED) {
            return nullptr;
        }
        return static_cast<TaskIpcPerTaskRaw*>(mapping_);
    }

    sem_t* PosixTaskIpcMapping::event_semaphore() const {
        return event_semaphore_;
    }

    sem_t* PosixTaskIpcMapping::cancel_semaphore() const {
        return cancel_semaphore_;
    }

    QString PosixTaskIpcMapping::shm_name() const {
        return shm_name_;
    }

    QString PosixTaskIpcMapping::sem_name() const {
        return sem_name_;
    }

    QString PosixTaskIpcMapping::cancel_sem_name() const {
        return cancel_sem_name_;
    }

    bool PosixTaskIpcMapping::start_owner_waiter(QString* error_message) {
        if (!unlink_on_destroy_ || waiter_started_) {
            return true;
        }
        if (event_semaphore_ == nullptr || event_semaphore_ == SEM_FAILED) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC event semaphore is unavailable.");
            }
            return false;
        }
        if (raw() == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC shared memory mapping is unavailable.");
            }
            return false;
        }

        stop_waiter_.store(false, std::memory_order_release);
        try {
            waiter_thread_ = std::thread([this]() { owner_wait_loop(); });
        } catch (std::system_error const& error) {
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
        if (platform_monitor_ == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC platform monitor is unavailable.");
            }
            return false;
        }
        return platform_monitor_->start_unclaimed_timer(
            kUnclaimedDispatchedReclaimMsecs, [this]() { handle_unclaimed_timer_event(); }, error_message);
    }

    bool PosixTaskIpcMapping::start_worker_exit_monitor(QString* error_message) {
        if (!unlink_on_destroy_) {
            return true;
        }
        TaskIpcPerTaskRaw* shared_raw = raw();
        if (shared_raw == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC shared memory mapping is unavailable.");
            }
            return false;
        }
        qint64 const worker_pid = std::atomic_ref<qint64>(shared_raw->slot.worker_pid).load(std::memory_order_acquire);
        if (worker_pid <= 0) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC worker PID is invalid.");
            }
            return false;
        }
        if (platform_monitor_ == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC platform monitor is unavailable.");
            }
            return false;
        }
        return platform_monitor_->start_worker_exit_monitor(
            worker_pid, [this]() { handle_worker_exit_event(); }, error_message);
    }

    void PosixTaskIpcMapping::stop_unclaimed_timer() {
        std::lock_guard<std::mutex> lock(unclaimed_stop_mutex_);
        if (platform_monitor_ != nullptr) {
            platform_monitor_->stop_unclaimed_timer();
        }
    }

    void PosixTaskIpcMapping::stop_worker_exit_monitor() {
        std::lock_guard<std::mutex> lock(worker_stop_mutex_);
        if (platform_monitor_ != nullptr) {
            platform_monitor_->stop_worker_exit_monitor();
        }
    }

    void PosixTaskIpcMapping::unlink_owned_names() {
        if (!unlink_on_destroy_) {
            return;
        }
        std::lock_guard<std::mutex> lock(unlink_mutex_);
        if (!shm_unlinked_) {
            shm_unlinked_ = cleanup_posix_unlink(posix_name_bytes(shm_name_), ::shm_unlink);
        }
        if (!sem_unlinked_) {
            sem_unlinked_ = cleanup_posix_unlink(posix_name_bytes(sem_name_), ::sem_unlink);
        }
        if (!cancel_sem_unlinked_) {
            cancel_sem_unlinked_ = cleanup_posix_unlink(posix_name_bytes(cancel_sem_name_), ::sem_unlink);
        }
    }

    void PosixTaskIpcMapping::unlink_names_after_claim() {
        TaskIpcPerTaskRaw* shared_raw = raw();
        if (!unlink_on_destroy_ || shared_raw == nullptr) {
            return;
        }
        quint32 const state = std::atomic_ref<quint32>(shared_raw->slot.state).load(std::memory_order_acquire);
        if (state == static_cast<quint32>(TaskIpcSlotState::kClaimed)
            || state == static_cast<quint32>(TaskIpcSlotState::kCompleted)) {
            stop_unclaimed_timer();
            unlink_owned_names();
        }
    }

    void PosixTaskIpcMapping::stop_owner_waiter() {
        if (!waiter_started_) {
            return;
        }

        stop_waiter_.store(true, std::memory_order_release);
        stop_unclaimed_timer();
        stop_worker_exit_monitor();
        if (event_semaphore_ != nullptr && event_semaphore_ != SEM_FAILED) {
            ::sem_post(event_semaphore_);
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

    void PosixTaskIpcMapping::handle_unclaimed_timer_event() {
        bool should_enqueue = false;
        TaskIpcPerTaskRaw* shared_raw = raw();
        if (shared_raw != nullptr) {
            SharedMemoryLock lock(&shared_raw->lock);
            if (lock.ok()) {
                should_enqueue = publish_task_ipc_unclaimed_timeout_completion(&shared_raw->slot, now_msecs());
            }
        }
        if (should_enqueue) {
            post_posix_task_notification(this, nullptr);
        }
    }

    void PosixTaskIpcMapping::handle_worker_exit_event() {
        bool should_enqueue = false;
        TaskIpcPerTaskRaw* shared_raw = raw();
        if (shared_raw != nullptr) {
            SharedMemoryLock lock(&shared_raw->lock);
            if (lock.ok()) {
                should_enqueue = publish_task_ipc_observed_worker_exit_completion(&shared_raw->slot, now_msecs());
            }
        }
        if (should_enqueue) {
            post_posix_task_cancel_notification(this, nullptr);
            post_posix_task_notification(this, nullptr);
        }
    }

    void PosixTaskIpcMapping::owner_wait_loop() {
        while (!stop_waiter_.load(std::memory_order_acquire)) {
            if (::sem_wait(event_semaphore_) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                notify_owner_or_defer();
                return;
            }

            if (stop_waiter_.load(std::memory_order_acquire)) {
                return;
            }
            unlink_names_after_claim();
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

    void register_posix_task_mapping(std::shared_ptr<PosixTaskIpcMapping> const& mapping) {
        if (mapping == nullptr || mapping->raw() == nullptr) {
            return;
        }
        static_cast<void>(posix_event_dispatcher());
        std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
        for (std::shared_ptr<PosixTaskIpcMapping> const& existing : posix_task_registry()) {
            if (existing.get() == mapping.get()) {
                return;
            }
        }
        posix_task_registry().push_back(mapping);
    }

    std::shared_ptr<PosixTaskIpcMapping> find_posix_task_mapping(quint64 session_id, quint32 generation) {
        std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
        for (std::shared_ptr<PosixTaskIpcMapping> const& mapping : posix_task_registry()) {
            TaskIpcPerTaskRaw const* shared_raw = mapping == nullptr ? nullptr : mapping->raw();
            if (shared_raw != nullptr
                && shared_raw->slot.session_id == session_id
                && shared_raw->slot.generation == generation) {
                return mapping;
            }
        }
        return nullptr;
    }

    QVector<std::shared_ptr<PosixTaskIpcMapping>> posix_task_mappings_snapshot() {
        std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
        return posix_task_registry();
    }

    void remove_posix_task_mapping(std::shared_ptr<PosixTaskIpcMapping> const& mapping) {
        if (mapping == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> lock(posix_task_registry_mutex());
        QVector<std::shared_ptr<PosixTaskIpcMapping>>& registry = posix_task_registry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if ((*it).get() == mapping.get()) {
                registry.erase(it);
                return;
            }
        }
    }

    void set_posix_worker_endpoint(QString const& shm_name, QString const& sem_name) {
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

    bool post_posix_task_notification(PosixTaskIpcMapping* mapping, QString* error_message) {
        if (mapping == nullptr || mapping->event_semaphore() == nullptr || mapping->event_semaphore() == SEM_FAILED) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC event semaphore is unavailable.");
            }
            return false;
        }
        if (::sem_post(mapping->event_semaphore()) == -1) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("sem_post(event)"));
            }
            return false;
        }
        return true;
    }

    bool post_posix_task_cancel_notification(PosixTaskIpcMapping* mapping, QString* error_message) {
        if (mapping == nullptr || mapping->cancel_semaphore() == nullptr || mapping->cancel_semaphore() == SEM_FAILED) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Task IPC cancel semaphore is unavailable.");
            }
            return false;
        }
        if (::sem_post(mapping->cancel_semaphore()) == -1) {
            if (error_message != nullptr) {
                *error_message = posix_errno_message(QStringLiteral("sem_post(cancel)"));
            }
            return false;
        }
        return true;
    }

    void set_posix_event_notifier(QString const& owner_instance_id, TaskIpcEventNotifier notifier) {
        QString const trimmed_owner = owner_instance_id.trimmed();
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

    void clear_posix_event_notifier(QString const& owner_instance_id) {
        QString const trimmed_owner = owner_instance_id.trimmed();
        if (trimmed_owner.isEmpty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(posix_event_notifier_mutex());
        posix_event_notifiers().remove(trimmed_owner);
    }

    TaskIpcEventNotifier posix_event_notifier_for_owner(QString const& owner_instance_id) {
        QString const trimmed_owner = owner_instance_id.trimmed();
        if (trimmed_owner.isEmpty()) {
            return TaskIpcEventNotifier();
        }

        std::lock_guard<std::mutex> lock(posix_event_notifier_mutex());
        return posix_event_notifiers().value(trimmed_owner);
    }

} // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
