#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#define Z7_TASK_IPC_PER_TASK_POSIX 1
#else
#define Z7_TASK_IPC_PER_TASK_POSIX 0
#endif

#if Z7_TASK_IPC_PER_TASK_POSIX
#include <semaphore.h>
#else
#include <QSystemSemaphore>
#endif

#include "native_process_qt.h"
#include "task_ipc_runtime.h"

class QSharedMemory;

namespace z7::task_ipc_runtime::task_ipc_internal {

    extern quint32 const kTaskIpcMagic;
    extern quint16 const kTaskIpcVersion;
    extern quint32 const kTaskIpcPayloadMagic;
    extern quint16 const kTaskIpcPayloadVersion;
    extern quint32 const kTaskIpcRequestPoolMagic;
    extern quint16 const kTaskIpcRequestPoolVersion;
    extern int const kTaskIpcSlotCount;
    extern int const kTaskIpcRequestPoolSlotSize;
    extern int const kTaskIpcRequestPoolSharedMemorySize;
    extern int const kWorkerClaimWaitMsecs;
    extern qint64 const kClaimableDispatchedAgeMsecs;
    extern qint64 const kUnclaimedDispatchedReclaimMsecs;
    extern int const kCompletionPublishWaitMsecs;
    extern qint64 const kCompletedOrphanReclaimMsecs;
    extern char const kTaskIpcOwnerIdProperty[];
    inline constexpr int kTaskIpcOwnerInstanceIdCapacity = 512;
    inline constexpr int kTaskIpcOwnerInstanceIdMaxUtf8Bytes = kTaskIpcOwnerInstanceIdCapacity - 1;
    inline constexpr quint32 kTaskIpcDispatchedEventSequence = 1U;
    inline constexpr quint32 kTaskIpcClaimedEventSequence = 2U;
    inline constexpr quint32 kTaskIpcCompletedEventSequence = 3U;

    struct RobustLockRaw {
        quint32 locker_pid = 0;
    };

    struct TaskIpcSlotRaw {
        quint32 generation = 0;
        quint32 state = static_cast<quint32>(TaskIpcSlotState::kEmpty);
        quint64 session_id = 0;
        quint32 command_kind = static_cast<quint32>(TaskIpcCommandKind::kNone);
        quint32 published_event_sequence = 0;
        quint32 acknowledged_event_sequence = 0;
        quint32 cancel_requested = 0;
        qint32 result_code = 0;
        quint32 refresh_after_finish = 1;
        qint64 launcher_pid = 0;
        qint64 worker_pid = 0;
        quint32 request_pool_slot = 0;
        quint32 request_payload_size = 0;
        qint64 updated_msecs = 0;
        char owner_instance_id[kTaskIpcOwnerInstanceIdCapacity]{};
        char summary[256]{};
    };

    struct TaskIpcBootstrapRaw {
        RobustLockRaw lock{};
        quint32 magic = 0;
        quint16 version = 0;
        quint16 slot_count = 0;
        quint64 next_session_id = 1;
        TaskIpcSlotRaw slot_records[16]{};
    };

    struct TaskIpcRequestPoolHeaderRaw {
        quint32 magic = 0;
        quint16 version = 0;
        quint16 slot_count = 0;
        quint16 reserved = 0;
        RobustLockRaw slot_locks[16]{};
    };

    inline constexpr int kTaskIpcRequestPoolPayloadOffset =
        ((static_cast<int>(sizeof(TaskIpcRequestPoolHeaderRaw)) + 63) / 64) * 64;

#if Z7_TASK_IPC_PER_TASK_POSIX
    inline constexpr quint16 kTaskIpcPerTaskVersion = 1;
    inline constexpr int kTaskIpcPerTaskPayloadCapacity = 1 * 1024 * 1024;
    inline constexpr int kTaskIpcPosixNameCapacity = 64;

    struct alignas(64) TaskIpcPerTaskRaw {
        RobustLockRaw lock{};
        quint32 magic = 0;
        quint16 version = 0;
        quint16 reserved = 0;
        char cancel_semaphore_name[kTaskIpcPosixNameCapacity]{};
        TaskIpcSlotRaw slot{};
        char payload[kTaskIpcPerTaskPayloadCapacity]{};
    };

    static_assert(std::atomic<quint32>::is_always_lock_free,
                  "Task IPC shared state requires lock-free 32-bit atomics.");
    static_assert(std::atomic<qint64>::is_always_lock_free, "Task IPC shared state requires lock-free 64-bit atomics.");
    static_assert(alignof(TaskIpcPerTaskRaw) >= 64, "Task IPC per-task shared state must preserve cache alignment.");
    static_assert(offsetof(TaskIpcPerTaskRaw, lock) == 0, "Unexpected TaskIpcPerTaskRaw lock offset.");
    static_assert(offsetof(TaskIpcPerTaskRaw, slot) % alignof(TaskIpcSlotRaw) == 0,
                  "Task IPC slot storage must be naturally aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, state) % alignof(quint32) == 0,
                  "Task IPC state field must be atomically aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, published_event_sequence) % alignof(quint32) == 0,
                  "Task IPC event sequence field must be atomically aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, acknowledged_event_sequence) % alignof(quint32) == 0,
                  "Task IPC event acknowledgement field must be atomically aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, cancel_requested) % alignof(quint32) == 0,
                  "Task IPC cancel field must be atomically aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, result_code) % alignof(qint32) == 0,
                  "Task IPC result field must be atomically aligned.");
    static_assert(offsetof(TaskIpcSlotRaw, updated_msecs) % alignof(qint64) == 0,
                  "Task IPC timestamp field must be atomically aligned.");
#endif

    class SharedMemoryLock {
    public:
        explicit SharedMemoryLock(RobustLockRaw* lock, QString wake_key = QString());
        ~SharedMemoryLock();

        SharedMemoryLock(SharedMemoryLock const&) = delete;
        SharedMemoryLock& operator=(SharedMemoryLock const&) = delete;

        bool ok() const;
        bool busy() const;
        QString error() const;

    private:
        RobustLockRaw* lock_ = nullptr;
        QString wake_key_;
        bool locked_ = false;
        bool busy_ = false;
        QString error_;
    };

    std::shared_ptr<QSharedMemory> current_bootstrap_memory_lease();
    void update_bootstrap_memory_lease(std::shared_ptr<QSharedMemory> const& memory);
    std::shared_ptr<QSharedMemory> current_request_pool_memory_lease();
    void update_request_pool_memory_lease(std::shared_ptr<QSharedMemory> const& memory);

    qint64 now_msecs();
    bool process_is_alive(qint64 pid);
    QString error_string_from_shared_memory(QSharedMemory* memory, QString const& fallback);
    std::unique_ptr<SharedMemoryLock> wait_for_shared_memory_lock(RobustLockRaw* lock,
                                                                  QString const& wake_key,
                                                                  int timeout_msecs,
                                                                  QString const& busy_message,
                                                                  QString* error_message);
    QString task_ipc_bootstrap_lock_wake_key();
    QString read_fixed_utf8(char const* bytes, int capacity);
    void write_fixed_utf8(QString const& value, char* out, int capacity);
    bool validate_task_ipc_owner_instance_id(QString const& owner_instance_id,
                                             QString* out_normalized_owner,
                                             QString* error_message);

    TaskIpcBootstrapRaw* bootstrap_raw(QSharedMemory* memory);
    TaskIpcBootstrapRaw const* bootstrap_raw(QSharedMemory const* memory);
    TaskIpcRequestPoolHeaderRaw* request_pool_raw(QSharedMemory* memory);
    TaskIpcRequestPoolHeaderRaw const* request_pool_raw(QSharedMemory const* memory);
    RobustLockRaw* request_pool_slot_lock(TaskIpcRequestPoolHeaderRaw* raw, int slot_index);
    RobustLockRaw const* request_pool_slot_lock(TaskIpcRequestPoolHeaderRaw const* raw, int slot_index);
    char* request_pool_slot_payload(TaskIpcRequestPoolHeaderRaw* raw, int slot_index);
    char const* request_pool_slot_payload(TaskIpcRequestPoolHeaderRaw const* raw, int slot_index);

    void clear_slot(TaskIpcSlotRaw* slot, bool bump_generation);
    quint32 task_ipc_event_sequence_for_kind(TaskIpcEventKind event_kind);
    TaskIpcEventKind task_ipc_event_kind_for_sequence(quint32 event_sequence);
    bool publish_task_ipc_stage_event(TaskIpcSlotRaw* slot, TaskIpcEventKind event_kind, qint64 now_msecs_value);
    bool publish_task_ipc_completion_event(TaskIpcSlotRaw* slot,
                                           int result_code,
                                           QString const& summary,
                                           qint64 now_msecs_value);
    bool publish_task_ipc_unclaimed_timeout_completion(TaskIpcSlotRaw* slot, qint64 now_msecs_value);
    bool publish_task_ipc_worker_exit_completion(TaskIpcSlotRaw* slot, qint64 now_msecs_value);
    bool publish_task_ipc_observed_worker_exit_completion(TaskIpcSlotRaw* slot, qint64 now_msecs_value);
    bool slot_has_pending_task_ipc_events(TaskIpcSlotRaw const& slot);
    quint32 next_pending_task_ipc_event_sequence(TaskIpcSlotRaw const& slot);
    void reclaim_stale_slots(TaskIpcBootstrapRaw* raw, qint64 now_msecs_value);

    bool initialize_bootstrap_if_needed(QSharedMemory* memory, bool force_init, QString* error_message);
    bool open_bootstrap_memory(bool allow_create, std::shared_ptr<QSharedMemory>* out_memory, QString* error_message);
    bool initialize_request_pool_if_needed(QSharedMemory* memory, bool force_init, QString* error_message);
    bool
    open_request_pool_memory(bool allow_create, std::shared_ptr<QSharedMemory>* out_memory, QString* error_message);

    bool slot_matches_owner(TaskIpcSlotRaw const& slot, QString const& owner_instance_id);
    bool slot_matches_claim(TaskIpcSlotRaw const& slot, TaskIpcClaimedTask const& task);
    bool slot_matches_event(TaskIpcSlotRaw const& slot, TaskIpcEvent const& event);

    QByteArray serialize_task_payload(TaskIpcPayload const& payload, QString* error_message);
    bool deserialize_task_payload(QByteArray const& encoded, TaskIpcPayload* out_payload, QString* error_message);
    bool write_request_payload_to_slot(QSharedMemory* request_pool_memory,
                                       int slot_index,
                                       QByteArray const& payload,
                                       QString* error_message);
    bool read_request_payload_from_slot(QSharedMemory* request_pool_memory,
                                        int slot_index,
                                        quint32 payload_size,
                                        TaskIpcPayload* out_payload,
                                        QString* error_message);
#if !Z7_TASK_IPC_PER_TASK_POSIX
    QString non_posix_task_ipc_event_semaphore_key(QString const& owner_instance_id);
    QString task_ipc_cancel_semaphore_key_for_task(quint64 session_id, quint32 generation);
    bool ensure_task_ipc_semaphore_exists(QString const& key, QString* error_message);
    bool post_task_ipc_semaphore(QString const& key, QString* error_message);
    bool set_non_posix_event_notifier(QString const& owner_instance_id,
                                      TaskIpcEventNotifier notifier,
                                      QString* error_message);
    void clear_non_posix_event_notifier(QString const& owner_instance_id);
#endif
    bool start_task_ipc_cancel_notification_thread(TaskIpcClaimedTask const& task,
                                                   TaskIpcCancelNotifier notifier,
                                                   QString* error_message);

#if Z7_TASK_IPC_PER_TASK_POSIX
    class PosixTaskIpcPlatformMonitor {
    public:
        virtual ~PosixTaskIpcPlatformMonitor() = default;

        virtual bool
        start_unclaimed_timer(qint64 timeout_msecs, std::function<void()> callback, QString* error_message) = 0;
        virtual void stop_unclaimed_timer() = 0;
    };

    std::unique_ptr<PosixTaskIpcPlatformMonitor> create_posix_task_ipc_platform_monitor();
    bool preflight_posix_task_ipc_platform(QString* error_message);

#if defined(Q_OS_LINUX)
    using LinuxPidfdOpenOverride = std::function<int(qint64)>;
    void set_linux_pidfd_open_override_for_test(LinuxPidfdOpenOverride override_function);
    void clear_linux_pidfd_open_override_for_test();
#endif

    class PosixTaskIpcMapping {
    public:
        ~PosixTaskIpcMapping();

        PosixTaskIpcMapping(PosixTaskIpcMapping const&) = delete;
        PosixTaskIpcMapping& operator=(PosixTaskIpcMapping const&) = delete;

        static std::shared_ptr<PosixTaskIpcMapping> create_owner(QByteArray const& payload,
                                                                 QString const& owner_instance_id,
                                                                 TaskIpcCommandKind command,
                                                                 bool refresh_after_finish,
                                                                 quint64 session_id,
                                                                 quint32 generation,
                                                                 QString* error_message);
        static std::shared_ptr<PosixTaskIpcMapping>
        open_worker(QString const& shm_name, QString const& sem_name, QString* error_message);

        TaskIpcPerTaskRaw* raw() const;
        sem_t* event_semaphore() const;
        sem_t* cancel_semaphore() const;
        QString shm_name() const;
        QString sem_name() const;
        QString cancel_sem_name() const;
        bool start_owner_waiter(QString* error_message);
        bool start_unclaimed_timer(QString* error_message);
        bool start_worker_exit_monitor(QString* error_message);
        void stop_unclaimed_timer();
        void stop_worker_exit_monitor();
        void unlink_names_after_claim();
        void enable_owner_notification_delivery();

    private:
        PosixTaskIpcMapping(QString shm_name,
                            QString sem_name,
                            QString cancel_sem_name,
                            int fd,
                            void* mapping,
                            sem_t* event_semaphore,
                            sem_t* cancel_semaphore,
                            QString owner_instance_id,
                            bool unlink_on_destroy);
        void stop_owner_waiter();
        void notify_owner_or_defer();
        void handle_unclaimed_timer_event();
        void handle_worker_exit_event();
        void owner_wait_loop();
        void unlink_owned_names();

        QString shm_name_;
        QString sem_name_;
        QString cancel_sem_name_;
        QString owner_instance_id_;
        int fd_ = -1;
        void* mapping_ = nullptr;
        sem_t* event_semaphore_ = SEM_FAILED;
        sem_t* cancel_semaphore_ = SEM_FAILED;
        bool unlink_on_destroy_ = false;
        bool waiter_started_ = false;
        bool shm_unlinked_ = false;
        bool sem_unlinked_ = false;
        bool cancel_sem_unlinked_ = false;
        std::mutex unlink_mutex_;
        std::mutex owner_notification_mutex_;
        bool owner_notification_delivery_enabled_ = false;
        bool pending_owner_notification_ = false;
        std::atomic_bool stop_waiter_{false};
        std::thread waiter_thread_;
        std::unique_ptr<PosixTaskIpcPlatformMonitor> platform_monitor_;
        std::unique_ptr<z7::platform::qt::NativeProcessExitMonitor> worker_exit_monitor_;
        std::mutex unclaimed_stop_mutex_;
        std::mutex worker_stop_mutex_;
    };

    quint64 next_posix_task_session_id();
    void register_posix_task_mapping(std::shared_ptr<PosixTaskIpcMapping> const& mapping);
    std::shared_ptr<PosixTaskIpcMapping> find_posix_task_mapping(quint64 session_id, quint32 generation);
    QVector<std::shared_ptr<PosixTaskIpcMapping>> posix_task_mappings_snapshot();
    void remove_posix_task_mapping(std::shared_ptr<PosixTaskIpcMapping> const& mapping);

    void set_posix_worker_endpoint(QString const& shm_name, QString const& sem_name);
    QString posix_worker_shm_name();
    QString posix_worker_sem_name();
    bool post_posix_task_notification(PosixTaskIpcMapping* mapping, QString* error_message);
    bool post_posix_task_cancel_notification(PosixTaskIpcMapping* mapping, QString* error_message);
    void set_posix_event_notifier(QString const& owner_instance_id, TaskIpcEventNotifier notifier);
    void clear_posix_event_notifier(QString const& owner_instance_id);
    TaskIpcEventNotifier posix_event_notifier_for_owner(QString const& owner_instance_id);
#endif

} // namespace z7::task_ipc_runtime::task_ipc_internal
