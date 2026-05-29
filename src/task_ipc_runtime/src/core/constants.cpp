#include "internal.h"

namespace z7::task_ipc_runtime::task_ipc_internal {

const quint32 kTaskIpcMagic = 0x5A374252U;  // "Z7BR"
const quint16 kTaskIpcVersion = 5;
const quint32 kTaskIpcPayloadMagic = 0x5A374250U;  // "Z7BP"
const quint16 kTaskIpcPayloadVersion = 8;
const quint32 kTaskIpcRequestPoolMagic = 0x5A375250U;  // "Z7RP"
const quint16 kTaskIpcRequestPoolVersion = 1;
const int kTaskIpcSlotCount = 16;
const int kTaskIpcRequestPoolSlotSize = 1 * 1024 * 1024;
const int kTaskIpcRequestPoolSharedMemorySize =
    kTaskIpcRequestPoolPayloadOffset +
    (kTaskIpcSlotCount * kTaskIpcRequestPoolSlotSize);
const int kWorkerClaimWaitMsecs = 8000;
const qint64 kClaimableDispatchedAgeMsecs = 15000;
const qint64 kUnclaimedDispatchedReclaimMsecs = 15000;
const int kCompletionPublishWaitMsecs = 2000;
const qint64 kCompletedOrphanReclaimMsecs = 3000;
const char kTaskIpcOwnerIdProperty[] = "z7.task_ipc.owner.instance.id";

}  // namespace z7::task_ipc_runtime::task_ipc_internal
