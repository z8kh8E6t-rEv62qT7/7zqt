#include "internal.h"

#if defined(Q_OS_MACOS)

#include <QCoreApplication>
#include <condition_variable>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <mutex>
#include <utility>

namespace z7::task_ipc_runtime::task_ipc_internal {
    namespace {

        class MacPosixTaskIpcPlatformMonitor final : public PosixTaskIpcPlatformMonitor {
        public:
            ~MacPosixTaskIpcPlatformMonitor() override { stop_unclaimed_timer(); }

            bool start_unclaimed_timer(qint64 timeout_msecs,
                                       std::function<void()> callback,
                                       QString* error_message) override {
                if (error_message != nullptr) {
                    error_message->clear();
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (source_ != nullptr) {
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

                callback_ = std::move(callback);
                cancel_complete_ = false;
                source_ = source;
                dispatch_set_context(source, this);
                dispatch_source_set_event_handler_f(source, &MacPosixTaskIpcPlatformMonitor::timer_event);
                dispatch_source_set_cancel_handler_f(source, &MacPosixTaskIpcPlatformMonitor::timer_cancel);
                dispatch_source_set_timer(
                    source,
                    dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeout_msecs) * NSEC_PER_MSEC),
                    DISPATCH_TIME_FOREVER,
                    0);
                dispatch_resume(source);
                return true;
            }

            void stop_unclaimed_timer() override {
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    source = source_;
                }
                if (source == nullptr) {
                    return;
                }

                dispatch_source_cancel(source);
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this]() { return cancel_complete_; });
                    if (source_ == source) {
                        source_ = nullptr;
                    }
                    cancel_complete_ = false;
                    callback_ = {};
                }
                dispatch_release(source);
            }

        private:
            static void timer_event(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self != nullptr) {
                    self->handle_event();
                }
            }

            static void timer_cancel(void* context) {
                auto* self = static_cast<MacPosixTaskIpcPlatformMonitor*>(context);
                if (self == nullptr) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(self->mutex_);
                    self->cancel_complete_ = true;
                }
                self->condition_.notify_all();
            }

            void handle_event() {
                std::function<void()> callback;
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    callback = callback_;
                    source = source_;
                }
                if (source != nullptr) {
                    dispatch_source_cancel(source);
                }
                if (callback) {
                    callback();
                }
            }

            std::mutex mutex_;
            std::condition_variable condition_;
            dispatch_source_t source_ = nullptr;
            std::function<void()> callback_;
            bool cancel_complete_ = false;
        };

    } // namespace

    std::unique_ptr<PosixTaskIpcPlatformMonitor> create_posix_task_ipc_platform_monitor() {
        return std::make_unique<MacPosixTaskIpcPlatformMonitor>();
    }

    bool preflight_posix_task_ipc_platform(QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        qint64 const current_pid = static_cast<qint64>(QCoreApplication::applicationPid());
        z7::platform::qt::NativeProcessSnapshot const snapshot = z7::platform::qt::native_process_snapshot();
        z7::platform::qt::NativeProcessInfo const* process = snapshot.ok ? snapshot.find_pid(current_pid) : nullptr;
        if (process == nullptr) {
            if (error_message != nullptr) {
                *error_message =
                    snapshot.ok ? QStringLiteral("Cannot inspect the current process.") : snapshot.error_message;
            }
            return false;
        }
        std::unique_ptr<z7::platform::qt::NativeProcessExitMonitor> monitor =
            z7::platform::qt::NativeProcessExitMonitor::create(process->identity, []() {}, error_message);
        if (monitor == nullptr) {
            return false;
        }
        monitor->cancel();
        return true;
    }

} // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
