#include "native_process_qt.h"

#if defined(Q_OS_WIN)

#include <QFileInfo>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

// clang-format off: Tool Help relies on Windows SDK types declared by windows.h.
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

namespace z7::platform::qt {
    namespace {

        quint64 file_time_token(FILETIME const& value) {
            ULARGE_INTEGER combined{};
            combined.LowPart = value.dwLowDateTime;
            combined.HighPart = value.dwHighDateTime;
            return combined.QuadPart;
        }

        QString windows_error_message(QString const& operation, DWORD error_code) {
            wchar_t* message = nullptr;
            DWORD const size = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error_code,
                0,
                reinterpret_cast<wchar_t*>(&message),
                0,
                nullptr);
            QString detail = size > 0 && message != nullptr ? QString::fromWCharArray(message, static_cast<int>(size))
                                                            : QStringLiteral("error %1").arg(error_code);
            if (message != nullptr) {
                LocalFree(message);
            }
            return QStringLiteral("%1 failed: %2").arg(operation, detail.trimmed());
        }

        QString process_path(HANDLE process) {
            std::wstring buffer(32768, L'\0');
            DWORD length = static_cast<DWORD>(buffer.size());
            if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) {
                return {};
            }
            return QFileInfo(QString::fromWCharArray(buffer.data(), static_cast<int>(length))).absoluteFilePath();
        }

        bool process_start_token(HANDLE process, quint64* output) {
            if (output == nullptr) {
                return false;
            }
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
                return false;
            }
            *output = file_time_token(creation);
            return true;
        }

        class WindowsNativeProcessExitMonitor final : public NativeProcessExitMonitor {
        public:
            ~WindowsNativeProcessExitMonitor() override { cancel(); }

            static std::unique_ptr<WindowsNativeProcessExitMonitor>
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

                HANDLE const process = OpenProcess(
                    SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(identity.pid));
                if (process == nullptr) {
                    if (error_message != nullptr) {
                        *error_message = windows_error_message(QStringLiteral("OpenProcess"), GetLastError());
                    }
                    return nullptr;
                }
                quint64 actual_start = 0;
                if (!process_start_token(process, &actual_start)
                    || (identity.start_time_token != 0 && actual_start != identity.start_time_token)) {
                    CloseHandle(process);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("The process is no longer running.");
                    }
                    return nullptr;
                }

                HANDLE const cancel_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (cancel_event == nullptr) {
                    DWORD const error_code = GetLastError();
                    CloseHandle(process);
                    if (error_message != nullptr) {
                        *error_message = windows_error_message(QStringLiteral("CreateEventW"), error_code);
                    }
                    return nullptr;
                }

                auto monitor = std::unique_ptr<WindowsNativeProcessExitMonitor>(new WindowsNativeProcessExitMonitor);
                monitor->process_ = process;
                monitor->cancel_event_ = cancel_event;
                monitor->callback_ = std::move(callback);
                try {
                    monitor->thread_ = std::thread([self = monitor.get()]() { self->wait_loop(); });
                } catch (std::system_error const& error) {
                    monitor->callback_ = {};
                    monitor->process_ = nullptr;
                    monitor->cancel_event_ = nullptr;
                    CloseHandle(cancel_event);
                    CloseHandle(process);
                    if (error_message != nullptr) {
                        *error_message = QStringLiteral("Failed to start a Windows process-exit monitor: %1")
                                             .arg(QString::fromLocal8Bit(error.what()));
                    }
                    return nullptr;
                }
                return monitor;
            }

            void cancel() override {
                HANDLE cancel_event = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (cancelled_) {
                        return;
                    }
                    cancelled_ = true;
                    callback_ = {};
                    cancel_event = cancel_event_;
                }
                if (cancel_event != nullptr) {
                    SetEvent(cancel_event);
                }
                if (thread_.joinable()) {
                    thread_.join();
                }
                if (cancel_event_ != nullptr) {
                    CloseHandle(cancel_event_);
                    cancel_event_ = nullptr;
                }
                if (process_ != nullptr) {
                    CloseHandle(process_);
                    process_ = nullptr;
                }
            }

        private:
            void wait_loop() {
                HANDLE handles[2] = {process_, cancel_event_};
                DWORD const result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) {
                    deliver();
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
            HANDLE process_ = nullptr;
            HANDLE cancel_event_ = nullptr;
            bool cancelled_ = false;
            bool delivered_ = false;
        };

    } // namespace

    NativeProcessSnapshot native_process_snapshot() {
        NativeProcessSnapshot snapshot;
        HANDLE const toolhelp = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (toolhelp == INVALID_HANDLE_VALUE) {
            snapshot.error_message = windows_error_message(QStringLiteral("CreateToolhelp32Snapshot"), GetLastError());
            return snapshot;
        }

        PROCESSENTRY32W process_entry{};
        process_entry.dwSize = sizeof(process_entry);
        if (!Process32FirstW(toolhelp, &process_entry)) {
            DWORD const error_code = GetLastError();
            CloseHandle(toolhelp);
            snapshot.error_message = windows_error_message(QStringLiteral("Process32FirstW"), error_code);
            return snapshot;
        }

        do {
            NativeProcessInfo entry;
            entry.identity.pid = static_cast<qint64>(process_entry.th32ProcessID);
            entry.parent_pid = static_cast<qint64>(process_entry.th32ParentProcessID);
            entry.executable_name = QString::fromWCharArray(process_entry.szExeFile);

            HANDLE const process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_entry.th32ProcessID);
            if (process != nullptr) {
                static_cast<void>(process_start_token(process, &entry.identity.start_time_token));
                entry.executable_path = process_path(process);
                if (!entry.executable_path.isEmpty()) {
                    entry.executable_name = QFileInfo(entry.executable_path).fileName();
                }
                CloseHandle(process);
            }
            snapshot.entries.push_back(std::move(entry));
        } while (Process32NextW(toolhelp, &process_entry));

        CloseHandle(toolhelp);
        snapshot.ok = true;
        return snapshot;
    }

    std::unique_ptr<NativeProcessExitMonitor>
    NativeProcessExitMonitor::create(NativeProcessIdentity identity, Callback callback, QString* error_message) {
        return WindowsNativeProcessExitMonitor::start(identity, std::move(callback), error_message);
    }

} // namespace z7::platform::qt

#endif
