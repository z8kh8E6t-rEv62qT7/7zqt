#include "internal.h"

#if defined(Q_OS_LINUX)

#include <QCoreApplication>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

namespace z7::task_ipc_runtime::task_ipc_internal {
    namespace {

        std::mutex& pidfd_override_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        LinuxPidfdOpenOverride& pidfd_open_override() {
            static LinuxPidfdOpenOverride override_function;
            return override_function;
        }

        int system_pidfd_open(qint64 pid) {
#if defined(SYS_pidfd_open)
            return static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0U));
#else
            Q_UNUSED(pid);
            errno = ENOSYS;
            return -1;
#endif
        }

        int open_pidfd(qint64 pid) {
            LinuxPidfdOpenOverride override_function;
            {
                std::lock_guard<std::mutex> lock(pidfd_override_mutex());
                override_function = pidfd_open_override();
            }
            return override_function ? override_function(pid) : system_pidfd_open(pid);
        }

        QString pidfd_error_message(QString const& operation, int error_number) {
            return QStringLiteral("%1 failed: %2 (Linux task IPC requires pidfd_open support).")
                .arg(operation, QString::fromLocal8Bit(std::strerror(error_number)));
        }

        class LinuxPosixTaskIpcPlatformMonitor final : public PosixTaskIpcPlatformMonitor {
        public:
            ~LinuxPosixTaskIpcPlatformMonitor() override {
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
                if (unclaimed_thread_.joinable()) {
                    return true;
                }

                unclaimed_stop_requested_ = false;
                unclaimed_callback_ = std::move(callback);
                try {
                    unclaimed_thread_ = std::thread([this, timeout_msecs]() {
                        std::function<void()> callback_to_run;
                        {
                            std::unique_lock<std::mutex> thread_lock(unclaimed_mutex_);
                            bool const stopped = unclaimed_cv_.wait_for(thread_lock,
                                                                        std::chrono::milliseconds(timeout_msecs),
                                                                        [this]() { return unclaimed_stop_requested_; });
                            if (!stopped) {
                                callback_to_run = unclaimed_callback_;
                            }
                        }
                        if (callback_to_run) {
                            callback_to_run();
                        }
                    });
                } catch (std::system_error const& error) {
                    unclaimed_callback_ = {};
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start task IPC unclaimed timer: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return false;
                }
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

                std::lock_guard<std::mutex> lock(worker_mutex_);
                if (worker_thread_.joinable()) {
                    return true;
                }

                int const pidfd = open_pidfd(worker_pid);
                if (pidfd == -1) {
                    int const error_number = errno;
                    if (error_message != nullptr) {
                        *error_message = pidfd_error_message(QStringLiteral("pidfd_open(worker)"), error_number);
                    }
                    return false;
                }

                int const stop_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (stop_fd == -1) {
                    int const error_number = errno;
                    ::close(pidfd);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("eventfd failed: %1")
                                             .arg(QString::fromLocal8Bit(std::strerror(error_number)));
                    }
                    return false;
                }

                worker_pidfd_ = pidfd;
                worker_stop_fd_ = stop_fd;
                worker_callback_ = std::move(callback);
                try {
                    worker_thread_ = std::thread([this, pidfd, stop_fd]() { worker_wait_loop(pidfd, stop_fd); });
                } catch (std::system_error const& error) {
                    worker_callback_ = {};
                    worker_pidfd_ = -1;
                    worker_stop_fd_ = -1;
                    ::close(stop_fd);
                    ::close(pidfd);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start task IPC pidfd monitor: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return false;
                }
                return true;
            }

            void stop_unclaimed_timer() override {
                {
                    std::lock_guard<std::mutex> lock(unclaimed_mutex_);
                    unclaimed_stop_requested_ = true;
                }
                unclaimed_cv_.notify_all();
                if (unclaimed_thread_.joinable()) {
                    unclaimed_thread_.join();
                }
                std::lock_guard<std::mutex> lock(unclaimed_mutex_);
                unclaimed_callback_ = {};
            }

            void stop_worker_exit_monitor() override {
                int stop_fd = -1;
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    stop_fd = worker_stop_fd_;
                }
                if (stop_fd != -1) {
                    std::uint64_t const value = 1;
                    static_cast<void>(::write(stop_fd, &value, sizeof(value)));
                }
                if (worker_thread_.joinable()) {
                    worker_thread_.join();
                }

                std::lock_guard<std::mutex> lock(worker_mutex_);
                if (worker_stop_fd_ != -1) {
                    ::close(worker_stop_fd_);
                    worker_stop_fd_ = -1;
                }
                if (worker_pidfd_ != -1) {
                    ::close(worker_pidfd_);
                    worker_pidfd_ = -1;
                }
                worker_callback_ = {};
            }

        private:
            void worker_wait_loop(int pidfd, int stop_fd) {
                pollfd descriptors[2] = {
                    pollfd{pidfd, POLLIN, 0},
                    pollfd{stop_fd, POLLIN, 0},
                };

                for (;;) {
                    int const result = ::poll(descriptors, 2, -1);
                    if (result == -1 && errno == EINTR) {
                        continue;
                    }
                    if (result == -1) {
                        invoke_worker_callback();
                        return;
                    }
                    if ((descriptors[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) {
                        return;
                    }
                    if ((descriptors[0].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) {
                        invoke_worker_callback();
                        return;
                    }
                }
            }

            void invoke_worker_callback() {
                std::function<void()> callback;
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    callback = worker_callback_;
                }
                if (callback) {
                    callback();
                }
            }

            std::mutex unclaimed_mutex_;
            std::condition_variable unclaimed_cv_;
            std::thread unclaimed_thread_;
            std::function<void()> unclaimed_callback_;
            bool unclaimed_stop_requested_ = false;

            std::mutex worker_mutex_;
            std::thread worker_thread_;
            std::function<void()> worker_callback_;
            int worker_pidfd_ = -1;
            int worker_stop_fd_ = -1;
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
        if (current_pid <= 0) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot validate pidfd support without a current process PID.");
            }
            return false;
        }

        int const pidfd = open_pidfd(current_pid);
        if (pidfd == -1) {
            int const error_number = errno;
            if (error_message != nullptr) {
                *error_message = pidfd_error_message(QStringLiteral("pidfd_open(preflight)"), error_number);
            }
            return false;
        }
        ::close(pidfd);
        return true;
    }

    void set_linux_pidfd_open_override_for_test(LinuxPidfdOpenOverride override_function) {
        std::lock_guard<std::mutex> lock(pidfd_override_mutex());
        pidfd_open_override() = std::move(override_function);
    }

    void clear_linux_pidfd_open_override_for_test() {
        std::lock_guard<std::mutex> lock(pidfd_override_mutex());
        pidfd_open_override() = {};
    }

} // namespace z7::task_ipc_runtime::task_ipc_internal

#endif
