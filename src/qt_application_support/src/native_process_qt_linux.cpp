#include "native_process_qt.h"

#if defined(Q_OS_LINUX)

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
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

namespace z7::platform::qt {
    namespace {

#if defined(Z7_TESTING)
        std::mutex& pidfd_override_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        LinuxPidfdOpenOverride& pidfd_open_override() {
            static LinuxPidfdOpenOverride override_function;
            return override_function;
        }
#endif

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
#if defined(Z7_TESTING)
            LinuxPidfdOpenOverride override_function;
            {
                std::lock_guard<std::mutex> lock(pidfd_override_mutex());
                override_function = pidfd_open_override();
            }
            if (override_function) {
                return override_function(pid);
            }
#endif
            return system_pidfd_open(pid);
        }

        QString system_error_message(QString const& operation, int error_number) {
            return QStringLiteral("%1 failed: %2").arg(operation, QString::fromLocal8Bit(std::strerror(error_number)));
        }

        bool read_process_info(qint64 pid, NativeProcessInfo* output) {
            if (output == nullptr || pid <= 0) {
                return false;
            }

            QFile stat_file(QStringLiteral("/proc/%1/stat").arg(pid));
            if (!stat_file.open(QIODevice::ReadOnly)) {
                return false;
            }
            QByteArray const stat = stat_file.readAll().trimmed();
            int const open_paren = stat.indexOf('(');
            int const close_paren = stat.lastIndexOf(')');
            if (open_paren < 0 || close_paren <= open_paren || close_paren + 2 > stat.size()) {
                return false;
            }

            QList<QByteArray> fields = stat.mid(close_paren + 2).split(' ');
            fields.removeAll(QByteArray{});
            if (fields.size() <= 19) {
                return false;
            }

            bool pid_ok = false;
            bool parent_ok = false;
            bool group_ok = false;
            bool start_ok = false;
            qint64 const parsed_pid = stat.left(open_paren).trimmed().toLongLong(&pid_ok);
            qint64 const parent_pid = fields[1].toLongLong(&parent_ok);
            qint64 const process_group_id = fields[2].toLongLong(&group_ok);
            quint64 const start_token = fields[19].toULongLong(&start_ok);
            if (!pid_ok || !parent_ok || !group_ok || !start_ok || parsed_pid != pid) {
                return false;
            }

            NativeProcessInfo entry;
            entry.identity.pid = parsed_pid;
            entry.identity.start_time_token = start_token;
            entry.parent_pid = parent_pid;
            entry.process_group_id = process_group_id;
            entry.executable_path = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
            if (!entry.executable_path.isEmpty()) {
                entry.executable_path = QFileInfo(entry.executable_path).absoluteFilePath();
                entry.executable_name = QFileInfo(entry.executable_path).fileName();
            } else {
                entry.executable_name = QString::fromLocal8Bit(stat.mid(open_paren + 1, close_paren - open_paren - 1));
            }
            *output = std::move(entry);
            return true;
        }

        class LinuxNativeProcessExitMonitor final : public NativeProcessExitMonitor {
        public:
            ~LinuxNativeProcessExitMonitor() override { cancel(); }

            static std::unique_ptr<LinuxNativeProcessExitMonitor>
            start(NativeProcessIdentity identity, Callback callback, QString* error_message) {
                if (error_message != nullptr) {
                    error_message->clear();
                }
                if (!identity.is_valid() || !callback) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Invalid native process exit-monitor request.");
                    }
                    return nullptr;
                }

                NativeProcessInfo process;
                if (!read_process_info(identity.pid, &process)
                    || (identity.start_time_token != 0 && process.identity != identity)) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("The process is no longer running.");
                    }
                    return nullptr;
                }

                int const pidfd = open_pidfd(identity.pid);
                if (pidfd == -1) {
                    int const error_number = errno;
                    if (error_message != nullptr) {
                        *error_message = system_error_message(QStringLiteral("pidfd_open"), error_number);
                    }
                    return nullptr;
                }
                int const cancel_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (cancel_fd == -1) {
                    int const error_number = errno;
                    ::close(pidfd);
                    if (error_message != nullptr) {
                        *error_message = system_error_message(QStringLiteral("eventfd"), error_number);
                    }
                    return nullptr;
                }

                NativeProcessInfo after_open;
                if (!read_process_info(identity.pid, &after_open)
                    || (identity.start_time_token != 0 && after_open.identity != identity)) {
                    ::close(cancel_fd);
                    ::close(pidfd);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("The process is no longer running.");
                    }
                    return nullptr;
                }

                auto monitor = std::unique_ptr<LinuxNativeProcessExitMonitor>(new LinuxNativeProcessExitMonitor);
                monitor->pidfd_ = pidfd;
                monitor->cancel_fd_ = cancel_fd;
                monitor->callback_ = std::move(callback);
                try {
                    monitor->thread_ = std::thread([self = monitor.get()]() { self->wait_loop(); });
                } catch (std::system_error const& error) {
                    monitor->callback_ = {};
                    monitor->pidfd_ = -1;
                    monitor->cancel_fd_ = -1;
                    ::close(cancel_fd);
                    ::close(pidfd);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start a Linux process-exit monitor: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return nullptr;
                }
                return monitor;
            }

            void cancel() override {
                int cancel_fd = -1;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (cancelled_) {
                        return;
                    }
                    cancelled_ = true;
                    callback_ = {};
                    cancel_fd = cancel_fd_;
                }
                if (cancel_fd != -1) {
                    std::uint64_t const value = 1;
                    static_cast<void>(::write(cancel_fd, &value, sizeof(value)));
                }
                if (thread_.joinable()) {
                    thread_.join();
                }
                if (cancel_fd_ != -1) {
                    ::close(cancel_fd_);
                    cancel_fd_ = -1;
                }
                if (pidfd_ != -1) {
                    ::close(pidfd_);
                    pidfd_ = -1;
                }
            }

        private:
            void wait_loop() {
                pollfd descriptors[2] = {
                    pollfd{pidfd_, POLLIN, 0},
                    pollfd{cancel_fd_, POLLIN, 0},
                };
                for (;;) {
                    int const result = ::poll(descriptors, 2, -1);
                    if (result == -1 && errno == EINTR) {
                        continue;
                    }
                    if (result == -1) {
                        // A monitor failure is not evidence that the process
                        // exited. Fail closed so owners retain their session.
                        return;
                    }
                    if ((descriptors[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) {
                        return;
                    }
                    if ((descriptors[0].revents & (POLLIN | POLLHUP)) != 0) {
                        deliver();
                        return;
                    }
                    if ((descriptors[0].revents & (POLLERR | POLLNVAL)) != 0) {
                        return;
                    }
                }
            }

            void deliver() {
                Callback callback;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (cancelled_ || delivered_) {
                        return;
                    }
                    delivered_ = true;
                    callback = std::move(callback_);
                }
                if (callback) {
                    callback();
                }
            }

            std::mutex mutex_;
            std::thread thread_;
            Callback callback_;
            int pidfd_ = -1;
            int cancel_fd_ = -1;
            bool cancelled_ = false;
            bool delivered_ = false;
        };

    } // namespace

    NativeProcessSnapshot native_process_snapshot() {
        NativeProcessSnapshot snapshot;
        QDir proc_dir(QStringLiteral("/proc"));
        if (!proc_dir.exists() || !proc_dir.isReadable()) {
            snapshot.error_message = QStringLiteral("The Linux /proc process filesystem is unavailable.");
            return snapshot;
        }

        QStringList const names = proc_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        snapshot.entries.reserve(names.size());
        for (QString const& name : names) {
            bool ok = false;
            qint64 const pid = name.toLongLong(&ok);
            if (!ok || pid <= 0) {
                continue;
            }
            NativeProcessInfo entry;
            if (read_process_info(pid, &entry)) {
                snapshot.entries.push_back(std::move(entry));
            }
        }
        snapshot.ok = true;
        return snapshot;
    }

    std::unique_ptr<NativeProcessExitMonitor>
    NativeProcessExitMonitor::create(NativeProcessIdentity identity, Callback callback, QString* error_message) {
        return LinuxNativeProcessExitMonitor::start(identity, std::move(callback), error_message);
    }

#if defined(Z7_TESTING)
    void set_linux_pidfd_open_override_for_test(LinuxPidfdOpenOverride override_function) {
        std::lock_guard<std::mutex> lock(pidfd_override_mutex());
        pidfd_open_override() = std::move(override_function);
    }

    void clear_linux_pidfd_open_override_for_test() {
        std::lock_guard<std::mutex> lock(pidfd_override_mutex());
        pidfd_open_override() = {};
    }
#endif

} // namespace z7::platform::qt

#endif
