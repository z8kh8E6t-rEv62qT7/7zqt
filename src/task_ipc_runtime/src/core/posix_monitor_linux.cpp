#include "internal.h"

#if defined(Q_OS_LINUX)

#include <QCoreApplication>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace z7::task_ipc_runtime::task_ipc_internal {
    namespace {

        class LinuxPosixTaskIpcPlatformMonitor final : public PosixTaskIpcPlatformMonitor {
        public:
            ~LinuxPosixTaskIpcPlatformMonitor() override { stop_unclaimed_timer(); }

            bool start_unclaimed_timer(qint64 timeout_msecs,
                                       std::function<void()> callback,
                                       QString* error_message) override {
                if (error_message != nullptr) {
                    error_message->clear();
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (thread_.joinable()) {
                    return true;
                }

                stop_requested_ = false;
                callback_ = std::move(callback);
                try {
                    thread_ = std::thread([this, timeout_msecs]() {
                        std::function<void()> callback_to_run;
                        {
                            std::unique_lock<std::mutex> thread_lock(mutex_);
                            bool const stopped = condition_.wait_for(thread_lock,
                                                                     std::chrono::milliseconds(timeout_msecs),
                                                                     [this]() { return stop_requested_; });
                            if (!stopped) {
                                callback_to_run = callback_;
                            }
                        }
                        if (callback_to_run) {
                            callback_to_run();
                        }
                    });
                } catch (std::system_error const& error) {
                    callback_ = {};
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start task IPC unclaimed timer: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return false;
                }
                return true;
            }

            void stop_unclaimed_timer() override {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stop_requested_ = true;
                }
                condition_.notify_all();
                if (thread_.joinable()) {
                    thread_.join();
                }
                std::lock_guard<std::mutex> lock(mutex_);
                callback_ = {};
            }

        private:
            std::mutex mutex_;
            std::condition_variable condition_;
            std::thread thread_;
            std::function<void()> callback_;
            bool stop_requested_ = false;
        };

    } // namespace

    std::unique_ptr<PosixTaskIpcPlatformMonitor> create_posix_task_ipc_platform_monitor() {
        return std::make_unique<LinuxPosixTaskIpcPlatformMonitor>();
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

#if defined(Z7_TESTING)
    void set_linux_pidfd_open_override_for_test(LinuxPidfdOpenOverride override_function) {
        z7::platform::qt::set_linux_pidfd_open_override_for_test(std::move(override_function));
    }

    void clear_linux_pidfd_open_override_for_test() {
        z7::platform::qt::clear_linux_pidfd_open_override_for_test();
    }
#endif

} // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
