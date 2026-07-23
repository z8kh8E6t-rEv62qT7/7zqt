#include "native_process_qt.h"

namespace z7::platform::qt {

    NativeProcessInfo const* NativeProcessSnapshot::find(NativeProcessIdentity const& identity) const {
        for (NativeProcessInfo const& entry : entries) {
            if (entry.identity == identity) {
                return &entry;
            }
        }
        return nullptr;
    }

    NativeProcessInfo const* NativeProcessSnapshot::find_pid(qint64 pid) const {
        for (NativeProcessInfo const& entry : entries) {
            if (entry.identity.pid == pid) {
                return &entry;
            }
        }
        return nullptr;
    }

} // namespace z7::platform::qt
