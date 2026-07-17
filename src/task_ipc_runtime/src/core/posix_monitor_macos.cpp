#include "internal.h"

#if defined(Q_OS_MACOS)

#include <condition_variable>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <mutex>
#include <utility>

namespace z7::task_ipc_runtime::task_ipc_internal {
    namespace {

        class MacPosixTaskIpcPlatformMonitor final : public PosixTaskIpcPlatformMonitor {
        public:
            ~MacPosixTaskIpcPlatformMonitor() override {
                stop_unclaimed_timer();
                stop_worker_exit_monitor();
            }

            bool start_unclaimed_timer(qint64 timeout_msecs,
                                       std::function<void()> callback,
                                       QString* error_message) override {
                if (error_message != nullptr) {
                    error_message->clear();
                }
                std::lock_guard<std::mutex> lock(unclaimed_mutex_);
                if (unclaimed_source_ != nullptr) {
                    return true;
                }

                dispatch_source_t source = dispatch_source_create(
                    DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
                if (source == nullptr) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to create task IPC unclaimed timer.");
                    }
                    return false;
                }

                unclaimed_callback_ = std::move(callback);
                unclaimed_cancel_complete_ = false;
                unclaimed_source_ = source;
                dispatch_set_context(source, this);
                dispatch_source_set_event_handler_f(source, &MacPosixTaskIpcPlatformMonitor::unclaimed_event);
                dispatch_source_set_cancel_handler_f(source, &MacPosixTaskIpcPlatformMonitor::unclaimed_cancel);
                dispatch_source_set_timer(
                    source,
                    dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeout_msecs) * NSEC_PER_MSEC),
                    DISPATCH_TIME_FOREVER,
                    0);
                dispatch_resume(source);
                return true;
            }

            bool start_worker_exit_monitor(qint64 worker_pid,
                                           std::function<void()> callback,
                                           QString* error_message) override {
                if (error_message != nullptr) {
                    error_message->clear();
                }
                if (worker_pid <= 0) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Task IPC worker PID is invalid.");
                    }
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    if (worker_source_ != nullptr) {
                        return true;
                    }

                    dispatch_source_t source =
                        dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC,
                                               static_cast<uintptr_t>(static_cast<pid_t>(worker_pid)),
                                               DISPATCH_PROC_EXIT,
                                               dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
                    if (source == nullptr) {
                        if (error_message != nullptr) {
                            *error_message = QStringLiteral("Failed to create task IPC worker-exit monitor.");
                        }
                        return false;
                    }

                    worker_callback_ = std::move(callback);
                    worker_cancel_complete_ = false;
                    worker_source_ = source;
                    dispatch_set_context(source, this);
                    dispatch_source_set_event_handler_f(source, &MacPosixTaskIpcPlatformMonitor::worker_event);
                    dispatch_source_set_cancel_handler_f(source, &MacPosixTaskIpcPlatformMonitor::worker_cancel);
                    dispatch_resume(source);
                }

                if (!process_is_alive(worker_pid)) {
                    handle_worker_event();
                }
                return true;
            }

            void stop_unclaimed_timer() override {
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(unclaimed_mutex_);
                    source = unclaimed_source_;
                }
                if (source == nullptr) {
                    return;
                }

                dispatch_source_cancel(source);
                {
                    std::unique_lock<std::mutex> lock(unclaimed_mutex_);
                    unclaimed_cv_.wait(lock, [this]() { return unclaimed_cancel_complete_; });
                    if (unclaimed_source_ == source) {
                        unclaimed_source_ = nullptr;
                    }
                    unclaimed_cancel_complete_ = false;
                    unclaimed_callback_ = {};
                }
                dispatch_release(source);
            }

            void stop_worker_exit_monitor() override {
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    source = worker_source_;
                }
                if (source == nullptr) {
                    return;
                }

                dispatch_source_cancel(source);
                {
                    std::unique_lock<std::mutex> lock(worker_mutex_);
                    worker_cv_.wait(lock, [this]() { return worker_cancel_complete_; });
                    if (worker_source_ == source) {
                        worker_source_ = nullptr;
                    }
                    worker_cancel_complete_ = false;
                    worker_callback_ = {};
                }
                dispatch_release(source);
            }

        private:
            static void unclaimed_event(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self != nullptr) {
                    self->handle_unclaimed_event();
                }
            }

            static void unclaimed_cancel(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self != nullptr) {
                    {
                        std::lock_guard<std::mutex> lock(self->unclaimed_mutex_);
                        self->unclaimed_cancel_complete_ = true;
                    }
                    self->unclaimed_cv_.notify_all();
                }
            }

            static void worker_event(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self != nullptr) {
                    self->handle_worker_event();
                }
            }

            static void worker_cancel(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self != nullptr) {
                    {
                        std::lock_guard<std::mutex> lock(self->worker_mutex_);
                        self->worker_cancel_complete_ = true;
                    }
                    self->worker_cv_.notify_all();
                }
            }

            void handle_unclaimed_event() {
                std::function<void()> callback;
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(unclaimed_mutex_);
                    callback = unclaimed_callback_;
                    source = unclaimed_source_;
                }
                if (source != nullptr) {
                    dispatch_source_cancel(source);
                }
                if (callback) {
                    callback();
                }
            }

            void handle_worker_event() {
                std::function<void()> callback;
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    callback = worker_callback_;
                    source = worker_source_;
                }
                if (source != nullptr) {
                    dispatch_source_cancel(source);
                }
                if (callback) {
                    callback();
                }
            }

            std::mutex unclaimed_mutex_;
            std::condition_variable unclaimed_cv_;
            dispatch_source_t unclaimed_source_ = nullptr;
            std::function<void()> unclaimed_callback_;
            bool unclaimed_cancel_complete_ = false;

            std::mutex worker_mutex_;
            std::condition_variable worker_cv_;
            dispatch_source_t worker_source_ = nullptr;
            std::function<void()> worker_callback_;
            bool worker_cancel_complete_ = false;
        };

    } // namespace

    std::unique_ptr<PosixTaskIpcPlatformMonitor> create_posix_task_ipc_platform_monitor() {
        return std::make_unique<MacPosixTaskIpcPlatformMonitor>();
    }

    bool preflight_posix_task_ipc_platform(QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

} // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
