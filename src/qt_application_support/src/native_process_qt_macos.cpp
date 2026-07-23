#include "native_process_qt.h"

#if defined(Q_OS_MACOS)

#include <QFileInfo>
#include <condition_variable>
#include <dispatch/dispatch.h>
#include <libproc.h>
#include <mutex>
#include <sys/proc_info.h>
#include <utility>
#include <vector>

namespace z7::platform::qt {
    namespace {

        quint64 start_time_token(proc_bsdinfo const& info) {
            return info.pbi_start_tvsec * UINT64_C(1000000) + info.pbi_start_tvusec;
        }

        QString process_path(pid_t pid) {
            std::vector<char> buffer(static_cast<size_t>(PROC_PIDPATHINFO_MAXSIZE), '\0');
            int const length = proc_pidpath(pid, buffer.data(), static_cast<uint32_t>(buffer.size()));
            if (length <= 0) {
                return {};
            }
            return QFileInfo(QString::fromUtf8(buffer.data(), length)).absoluteFilePath();
        }

        class MacNativeProcessExitMonitor final : public NativeProcessExitMonitor {
        public:
            ~MacNativeProcessExitMonitor() override { cancel(); }

            static std::unique_ptr<MacNativeProcessExitMonitor>
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

                NativeProcessSnapshot const before = native_process_snapshot();
                if (!before.ok) {
                    if (error_message != nullptr) {
                        *error_message = before.error_message;
                    }
                    return nullptr;
                }
                NativeProcessInfo const* running =
                    identity.start_time_token == 0 ? before.find_pid(identity.pid) : before.find(identity);
                if (running == nullptr) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("The process is no longer running.");
                    }
                    return nullptr;
                }

                auto monitor = std::unique_ptr<MacNativeProcessExitMonitor>(new MacNativeProcessExitMonitor);
                if (!monitor->start_source(identity, std::move(callback), error_message)) {
                    return nullptr;
                }

                NativeProcessSnapshot const after = native_process_snapshot();
                running = after.ok
                            ? (identity.start_time_token == 0 ? after.find_pid(identity.pid) : after.find(identity))
                            : nullptr;
                if (after.ok && running == nullptr) {
                    monitor->handle_event();
                }
                return monitor;
            }

            void cancel() override {
                dispatch_source_t source = nullptr;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (cancel_in_progress_) {
                        condition_.wait(lock, [this]() { return source_ == nullptr; });
                        return;
                    }
                    source = source_;
                    if (source == nullptr) {
                        return;
                    }
                    cancel_in_progress_ = true;
                    cancel_requested_ = true;
                }

                dispatch_source_cancel(source);
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this]() { return cancel_complete_; });
                    callback_ = {};
                    source_ = nullptr;
                    cancel_in_progress_ = false;
                }
                condition_.notify_all();
                dispatch_release(source);
            }

        private:
            bool start_source(NativeProcessIdentity identity, Callback callback, QString* error_message) {
                dispatch_source_t source =
                    dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC,
                                           static_cast<uintptr_t>(static_cast<pid_t>(identity.pid)),
                                           DISPATCH_PROC_EXIT,
                                           dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
                if (source == nullptr) {
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to create a macOS process-exit monitor.");
                    }
                    return false;
                }

                callback_ = std::move(callback);
                source_ = source;
                dispatch_set_context(source, this);
                dispatch_source_set_event_handler_f(source, &MacNativeProcessExitMonitor::event_handler);
                dispatch_source_set_cancel_handler_f(source, &MacNativeProcessExitMonitor::cancel_handler);
                dispatch_resume(source);
                return true;
            }

            static void event_handler(void* context) {
                auto* self = static_cast<MacNativeProcessExitMonitor*>(context);
                if (self != nullptr) {
                    self->handle_event();
                }
            }

            static void cancel_handler(void* context) {
                auto* self = static_cast<MacNativeProcessExitMonitor*>(context);
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
                Callback callback;
                dispatch_source_t source = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (delivered_ || cancel_requested_) {
                        return;
                    }
                    delivered_ = true;
                    callback = std::move(callback_);
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
            Callback callback_;
            bool delivered_ = false;
            bool cancel_requested_ = false;
            bool cancel_complete_ = false;
            bool cancel_in_progress_ = false;
        };

    } // namespace

    NativeProcessSnapshot native_process_snapshot() {
        NativeProcessSnapshot snapshot;
        int const required_bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (required_bytes <= 0) {
            snapshot.error_message = QStringLiteral("Failed to enumerate macOS processes.");
            return snapshot;
        }

        std::vector<pid_t> pids(static_cast<size_t>(required_bytes / static_cast<int>(sizeof(pid_t)) + 64), 0);
        int const actual_bytes =
            proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
        if (actual_bytes < 0) {
            snapshot.error_message = QStringLiteral("Failed to read the macOS process list.");
            return snapshot;
        }

        int const count = actual_bytes / static_cast<int>(sizeof(pid_t));
        snapshot.entries.reserve(count);
        for (int index = 0; index < count; ++index) {
            pid_t const pid = pids[static_cast<size_t>(index)];
            if (pid <= 0) {
                continue;
            }

            proc_bsdinfo info{};
            int const info_size = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
            if (info_size != static_cast<int>(sizeof(info))) {
                continue;
            }

            NativeProcessInfo entry;
            entry.identity.pid = static_cast<qint64>(info.pbi_pid);
            entry.identity.start_time_token = start_time_token(info);
            entry.parent_pid = static_cast<qint64>(info.pbi_ppid);
            entry.process_group_id = static_cast<qint64>(info.pbi_pgid);
            entry.executable_path = process_path(pid);
            if (!entry.executable_path.isEmpty()) {
                entry.executable_name = QFileInfo(entry.executable_path).fileName();
            } else if (info.pbi_name[0] != '\0') {
                entry.executable_name = QString::fromUtf8(info.pbi_name);
            } else {
                entry.executable_name = QString::fromUtf8(info.pbi_comm);
            }
            snapshot.entries.push_back(std::move(entry));
        }
        snapshot.ok = true;
        return snapshot;
    }

    std::unique_ptr<NativeProcessExitMonitor>
    NativeProcessExitMonitor::create(NativeProcessIdentity identity, Callback callback, QString* error_message) {
        return MacNativeProcessExitMonitor::start(identity, std::move(callback), error_message);
    }

} // namespace z7::platform::qt

#endif
