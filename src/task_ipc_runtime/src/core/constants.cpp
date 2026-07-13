#include "internal.h"

namespace z7::task_ipc_runtime::task_ipc_internal {

    quint32 const kTaskIpcMagic = 0x5A374252U; // "Z7BR"
    quint16 const kTaskIpcVersion = 5;
    quint32 const kTaskIpcPayloadMagic = 0x5A374250U; // "Z7BP"
    quint16 const kTaskIpcPayloadVersion = 9;
    quint32 const kTaskIpcRequestPoolMagic = 0x5A375250U; // "Z7RP"
    quint16 const kTaskIpcRequestPoolVersion = 1;
    int const kTaskIpcSlotCount = 16;
    int const kTaskIpcRequestPoolSlotSize = 1 * 1024 * 1024;
    int const kTaskIpcRequestPoolSharedMemorySize =
        kTaskIpcRequestPoolPayloadOffset + (kTaskIpcSlotCount * kTaskIpcRequestPoolSlotSize);
    int const kWorkerClaimWaitMsecs = 8000;
    qint64 const kClaimableDispatchedAgeMsecs = 15000;
    qint64 const kUnclaimedDispatchedReclaimMsecs = 15000;
    int const kCompletionPublishWaitMsecs = 2000;
    qint64 const kCompletedOrphanReclaimMsecs = 3000;
    char const kTaskIpcOwnerIdProperty[] = "z7.task_ipc.owner.instance.id";

} // namespace z7::task_ipc_runtime::task_ipc_internal
