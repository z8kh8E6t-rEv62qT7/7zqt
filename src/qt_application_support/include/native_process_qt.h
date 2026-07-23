#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace z7::platform::qt {

    struct NativeProcessIdentity {
        qint64 pid = 0;
        quint64 start_time_token = 0;

        bool is_valid() const { return pid > 0 && start_time_token != 0; }

        friend bool operator==(NativeProcessIdentity const&, NativeProcessIdentity const&) = default;
    };

    struct NativeProcessInfo {
        NativeProcessIdentity identity;
        qint64 parent_pid = 0;
        qint64 process_group_id = 0;
        QString executable_path;
        QString executable_name;
    };

    struct NativeProcessSnapshot {
        bool ok = false;
        QVector<NativeProcessInfo> entries;
        QString error_message;

        NativeProcessInfo const* find(NativeProcessIdentity const& identity) const;
        NativeProcessInfo const* find_pid(qint64 pid) const;
    };

    NativeProcessSnapshot native_process_snapshot();

    class NativeProcessExitMonitor {
    public:
        using Callback = std::function<void()>;

        virtual ~NativeProcessExitMonitor() = default;

        NativeProcessExitMonitor(NativeProcessExitMonitor const&) = delete;
        NativeProcessExitMonitor& operator=(NativeProcessExitMonitor const&) = delete;

        // The callback is delivered at most once and can run on a platform
        // worker thread. Destroying the monitor cancels future delivery.
        virtual void cancel() = 0;

        static std::unique_ptr<NativeProcessExitMonitor>
        create(NativeProcessIdentity identity, Callback callback, QString* error_message = nullptr);

    protected:
        NativeProcessExitMonitor() = default;
    };

#if defined(Z7_TESTING) && defined(Q_OS_LINUX)
    using LinuxPidfdOpenOverride = std::function<int(qint64)>;
    void set_linux_pidfd_open_override_for_test(LinuxPidfdOpenOverride override_function);
    void clear_linux_pidfd_open_override_for_test();
#endif

} // namespace z7::platform::qt
