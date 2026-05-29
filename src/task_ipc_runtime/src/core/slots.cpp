#include "internal.h"

#include <QSharedMemory>

#include <cstring>

namespace z7::task_ipc_runtime::task_ipc_internal {

TaskIpcBootstrapRaw* bootstrap_raw(QSharedMemory* memory) {
  if (memory == nullptr) {
    return nullptr;
  }
  return static_cast<TaskIpcBootstrapRaw*>(memory->data());
}

const TaskIpcBootstrapRaw* bootstrap_raw(const QSharedMemory* memory) {
  if (memory == nullptr) {
    return nullptr;
  }
  return static_cast<const TaskIpcBootstrapRaw*>(memory->constData());
}

TaskIpcRequestPoolHeaderRaw* request_pool_raw(QSharedMemory* memory) {
  if (memory == nullptr) {
    return nullptr;
  }
  return static_cast<TaskIpcRequestPoolHeaderRaw*>(memory->data());
}

const TaskIpcRequestPoolHeaderRaw* request_pool_raw(
    const QSharedMemory* memory) {
  if (memory == nullptr) {
    return nullptr;
  }
  return static_cast<const TaskIpcRequestPoolHeaderRaw*>(memory->constData());
}

RobustLockRaw* request_pool_slot_lock(TaskIpcRequestPoolHeaderRaw* raw,
                                      int slot_index) {
  if (raw == nullptr || slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    return nullptr;
  }
  return &raw->slot_locks[slot_index];
}

const RobustLockRaw* request_pool_slot_lock(
    const TaskIpcRequestPoolHeaderRaw* raw, int slot_index) {
  if (raw == nullptr || slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    return nullptr;
  }
  return &raw->slot_locks[slot_index];
}

char* request_pool_slot_payload(TaskIpcRequestPoolHeaderRaw* raw,
                                int slot_index) {
  if (raw == nullptr || slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    return nullptr;
  }
  char* base = reinterpret_cast<char*>(raw) + kTaskIpcRequestPoolPayloadOffset;
  return base + (slot_index * kTaskIpcRequestPoolSlotSize);
}

const char* request_pool_slot_payload(const TaskIpcRequestPoolHeaderRaw* raw,
                                      int slot_index) {
  if (raw == nullptr || slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    return nullptr;
  }
  const char* base =
      reinterpret_cast<const char*>(raw) + kTaskIpcRequestPoolPayloadOffset;
  return base + (slot_index * kTaskIpcRequestPoolSlotSize);
}

void clear_slot(TaskIpcSlotRaw* slot, bool bump_generation) {
  if (slot == nullptr) {
    return;
  }
  quint32 generation = slot->generation;
  if (bump_generation) {
    ++generation;
    if (generation == 0U) {
      generation = 1U;
    }
  }
  std::memset(slot, 0, sizeof(TaskIpcSlotRaw));
  slot->generation = generation;
  slot->state = static_cast<quint32>(TaskIpcSlotState::kEmpty);
}

quint32 task_ipc_event_sequence_for_kind(TaskIpcEventKind event_kind) {
  switch (event_kind) {
    case TaskIpcEventKind::kDispatched:
      return kTaskIpcDispatchedEventSequence;
    case TaskIpcEventKind::kClaimed:
      return kTaskIpcClaimedEventSequence;
    case TaskIpcEventKind::kCompleted:
      return kTaskIpcCompletedEventSequence;
    case TaskIpcEventKind::kNone:
    default:
      return 0U;
  }
}

TaskIpcEventKind task_ipc_event_kind_for_sequence(quint32 event_sequence) {
  switch (event_sequence) {
    case kTaskIpcDispatchedEventSequence:
      return TaskIpcEventKind::kDispatched;
    case kTaskIpcClaimedEventSequence:
      return TaskIpcEventKind::kClaimed;
    case kTaskIpcCompletedEventSequence:
      return TaskIpcEventKind::kCompleted;
    default:
      return TaskIpcEventKind::kNone;
  }
}

bool publish_task_ipc_stage_event(TaskIpcSlotRaw* slot,
                                  TaskIpcEventKind event_kind,
                                  qint64 now_msecs_value) {
  if (slot == nullptr) {
    return false;
  }
  const quint32 event_sequence = task_ipc_event_sequence_for_kind(event_kind);
  if (event_sequence == 0U || event_sequence <= slot->published_event_sequence) {
    return false;
  }
  slot->published_event_sequence = event_sequence;
  slot->updated_msecs = now_msecs_value;
  return true;
}

bool publish_task_ipc_completion_event(TaskIpcSlotRaw* slot, int result_code,
                                       const QString& summary,
                                       qint64 now_msecs_value) {
  if (slot == nullptr) {
    return false;
  }
  slot->state = static_cast<quint32>(TaskIpcSlotState::kCompleted);
  slot->result_code = result_code;
  write_fixed_utf8(summary, slot->summary, 256);
  return publish_task_ipc_stage_event(slot, TaskIpcEventKind::kCompleted,
                                      now_msecs_value);
}

bool publish_task_ipc_unclaimed_timeout_completion(TaskIpcSlotRaw* slot,
                                                   qint64 now_msecs_value) {
  if (slot == nullptr ||
      slot->state != static_cast<quint32>(TaskIpcSlotState::kDispatched) ||
      slot->published_event_sequence >= kTaskIpcCompletedEventSequence ||
      (now_msecs_value - slot->updated_msecs) < kUnclaimedDispatchedReclaimMsecs) {
    return false;
  }
  return publish_task_ipc_completion_event(
      slot, 2,
      QStringLiteral("7zG worker did not claim task before timing out."),
      now_msecs_value);
}

bool publish_task_ipc_worker_exit_completion(TaskIpcSlotRaw* slot,
                                             qint64 now_msecs_value) {
  if (slot == nullptr ||
      slot->state != static_cast<quint32>(TaskIpcSlotState::kClaimed) ||
      slot->published_event_sequence >= kTaskIpcCompletedEventSequence ||
      slot->worker_pid <= 0 || process_is_alive(slot->worker_pid)) {
    return false;
  }
  return publish_task_ipc_completion_event(
      slot, 2,
      QStringLiteral("7zG worker exited before publishing completion."),
      now_msecs_value);
}

bool slot_has_pending_task_ipc_events(const TaskIpcSlotRaw& slot) {
  return slot.acknowledged_event_sequence < slot.published_event_sequence;
}

quint32 next_pending_task_ipc_event_sequence(const TaskIpcSlotRaw& slot) {
  if (!slot_has_pending_task_ipc_events(slot)) {
    return 0U;
  }
  return slot.acknowledged_event_sequence + 1U;
}

void reclaim_stale_slots(TaskIpcBootstrapRaw* raw, qint64 now_msecs_value) {
  if (raw == nullptr) {
    return;
  }
  Q_UNUSED(now_msecs_value);

  for (int i = 0; i < kTaskIpcSlotCount; ++i) {
    TaskIpcSlotRaw& slot = raw->slot_records[i];
    if (slot.state != static_cast<quint32>(TaskIpcSlotState::kCompleted)) {
      continue;
    }
    if (slot.acknowledged_event_sequence >= kTaskIpcCompletedEventSequence) {
      clear_slot(&slot, true);
    }
  }
}

bool slot_matches_owner(const TaskIpcSlotRaw& slot,
                        const QString& owner_instance_id) {
  return read_fixed_utf8(slot.owner_instance_id,
                         kTaskIpcOwnerInstanceIdCapacity) ==
         owner_instance_id;
}

bool slot_matches_claim(const TaskIpcSlotRaw& slot,
                        const TaskIpcClaimedTask& task) {
  return task.slot_index >= 0 && slot.session_id == task.session_id &&
         slot.generation == task.generation;
}

bool slot_matches_event(const TaskIpcSlotRaw& slot, const TaskIpcEvent& event) {
  return slot.session_id == event.session_id &&
         slot.generation == event.generation &&
         slot_matches_owner(slot, event.owner_instance_id);
}

}  // namespace z7::task_ipc_runtime::task_ipc_internal
