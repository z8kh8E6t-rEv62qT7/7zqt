#include "native_process_qt.h"

namespace z7::platform::qt {

    NativeProcessSnapshot native_process_snapshot() {
        NativeProcessSnapshot snapshot;
        snapshot.error_message = QStringLiteral("Native process tracking is not supported on this platform.");
        return snapshot;
    }

    std::unique_ptr<NativeProcessExitMonitor>
    NativeProcessExitMonitor::create(NativeProcessIdentity, Callback, QString* error_message) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Native process tracking is not supported on this platform.");
        }
        return nullptr;
    }

} // namespace z7::platform::qt
