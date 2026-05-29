#include "task_ipc_runtime_internal.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QSharedMemory>

#include <cstring>

namespace z7::task_ipc_runtime::task_ipc_internal {

namespace {

template <typename TPayload>
void write_optional_presence(QDataStream& stream,
                             const std::optional<TPayload>& payload) {
  stream << payload.has_value();
}

template <typename TPayload>
bool read_optional_presence(QDataStream& stream,
                            bool* has_value) {
  if (has_value == nullptr) {
    return false;
  }
  *has_value = false;
  stream >> *has_value;
  return stream.status() == QDataStream::Ok;
}

quint32 encode_extract_path_remap_match_kind(
    TaskIpcExtractPathRemapMatchKind kind) {
  return static_cast<quint32>(kind);
}

TaskIpcExtractPathRemapMatchKind decode_extract_path_remap_match_kind(
    quint32 encoded) {
  switch (encoded) {
    case 1:
      return TaskIpcExtractPathRemapMatchKind::kExactArchivePath;
    case 2:
      return TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
    case 0:
    default:
      return TaskIpcExtractPathRemapMatchKind::kRequestRoot;
  }
}

void write_extract_path_remaps(QDataStream& stream,
                               const QVector<TaskIpcExtractPathRemap>& remaps) {
  stream << static_cast<quint32>(remaps.size());
  for (const TaskIpcExtractPathRemap& remap : remaps) {
    stream << encode_extract_path_remap_match_kind(remap.match_kind);
    stream << remap.source_path;
    stream << remap.destination_path;
  }
}

bool read_extract_path_remaps(QDataStream& stream,
                              QVector<TaskIpcExtractPathRemap>* out_remaps) {
  if (out_remaps == nullptr) {
    return false;
  }
  out_remaps->clear();

  quint32 count = 0;
  stream >> count;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  out_remaps->reserve(static_cast<int>(count));
  for (quint32 i = 0; i < count; ++i) {
    quint32 match_kind = 0;
    TaskIpcExtractPathRemap remap;
    stream >> match_kind;
    stream >> remap.source_path;
    stream >> remap.destination_path;
    if (stream.status() != QDataStream::Ok) {
      return false;
    }
    remap.match_kind = decode_extract_path_remap_match_kind(match_kind);
    out_remaps->push_back(std::move(remap));
  }
  return true;
}

void write_add_payload(QDataStream& stream,
                       const std::optional<TaskIpcAddPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->archive_path;
  stream << payload->archive_type;
  stream << payload->update_mode;
  stream << payload->raw_update_switch;
  stream << payload->raw_update_switches;
  stream << payload->path_mode;
  stream << payload->create_sfx;
  stream << payload->share_for_write;
  stream << payload->delete_after_compressing;
  stream << payload->send_by_email;
  stream << payload->send_by_email_remove_after;
  stream << payload->send_by_email_address;
  stream << payload->compression_level;
  stream << payload->method_value;
  stream << payload->dictionary_size;
  stream << payload->word_size;
  stream << payload->solid_block_size;
  stream << payload->thread_count;
  stream << payload->volume_size;
  stream << payload->password;
  stream << payload->encrypt_headers_defined;
  stream << payload->encrypt_headers;
  stream << payload->encryption_method;
  stream << payload->extra_parameters;
  stream << payload->input_paths;
}

bool read_add_payload(QDataStream& stream,
                      std::optional<TaskIpcAddPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcAddPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcAddPayload payload;
  stream >> payload.archive_path;
  stream >> payload.archive_type;
  stream >> payload.update_mode;
  stream >> payload.raw_update_switch;
  stream >> payload.raw_update_switches;
  stream >> payload.path_mode;
  stream >> payload.create_sfx;
  stream >> payload.share_for_write;
  stream >> payload.delete_after_compressing;
  stream >> payload.send_by_email;
  stream >> payload.send_by_email_remove_after;
  stream >> payload.send_by_email_address;
  stream >> payload.compression_level;
  stream >> payload.method_value;
  stream >> payload.dictionary_size;
  stream >> payload.word_size;
  stream >> payload.solid_block_size;
  stream >> payload.thread_count;
  stream >> payload.volume_size;
  stream >> payload.password;
  stream >> payload.encrypt_headers_defined;
  stream >> payload.encrypt_headers;
  stream >> payload.encryption_method;
  stream >> payload.extra_parameters;
  stream >> payload.input_paths;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_extract_payload(QDataStream& stream,
                           const std::optional<TaskIpcExtractPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->output_dir;
  stream << payload->split_dest_enabled;
  stream << payload->split_dest_name;
  stream << payload->overwrite_switch;
  stream << payload->archive_type;
  stream << payload->eliminate_root_duplication;
  write_extract_path_remaps(stream, payload->path_remaps);
  stream << payload->restore_file_security;
  stream << payload->zone_id_mode;
  stream << payload->password;
  stream << payload->archive_inputs;
}

bool read_extract_payload(QDataStream& stream,
                          std::optional<TaskIpcExtractPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcExtractPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcExtractPayload payload;
  stream >> payload.output_dir;
  stream >> payload.split_dest_enabled;
  stream >> payload.split_dest_name;
  stream >> payload.overwrite_switch;
  stream >> payload.archive_type;
  stream >> payload.eliminate_root_duplication;
  if (!read_extract_path_remaps(stream, &payload.path_remaps)) {
    return false;
  }
  stream >> payload.restore_file_security;
  stream >> payload.zone_id_mode;
  stream >> payload.password;
  stream >> payload.archive_inputs;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_archive_export_payload(
    QDataStream& stream,
    const std::optional<TaskIpcArchiveExportPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->root_archive_path;
  stream << payload->root_archive_type;
  stream << payload->nested_archive_entries;
  stream << payload->archive_entry_paths;
  stream << payload->output_dir;
  stream << payload->overwrite_mode;
  stream << payload->path_mode;
  stream << payload->eliminate_root_duplication;
  write_extract_path_remaps(stream, payload->path_remaps);
  stream << payload->restore_file_security;
  stream << payload->zone_id_mode;
  stream << payload->password;
}

bool read_archive_export_payload(
    QDataStream& stream,
    std::optional<TaskIpcArchiveExportPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcArchiveExportPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcArchiveExportPayload payload;
  stream >> payload.root_archive_path;
  stream >> payload.root_archive_type;
  stream >> payload.nested_archive_entries;
  stream >> payload.archive_entry_paths;
  stream >> payload.output_dir;
  stream >> payload.overwrite_mode;
  stream >> payload.path_mode;
  stream >> payload.eliminate_root_duplication;
  if (!read_extract_path_remaps(stream, &payload.path_remaps)) {
    return false;
  }
  stream >> payload.restore_file_security;
  stream >> payload.zone_id_mode;
  stream >> payload.password;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_test_payload(QDataStream& stream,
                        const std::optional<TaskIpcTestPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->archive_inputs;
}

bool read_test_payload(QDataStream& stream,
                       std::optional<TaskIpcTestPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcTestPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcTestPayload payload;
  stream >> payload.archive_inputs;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_hash_payload(QDataStream& stream,
                        const std::optional<TaskIpcHashPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->hash_method;
  stream << payload->input_paths;
}

bool read_hash_payload(QDataStream& stream,
                       std::optional<TaskIpcHashPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcHashPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcHashPayload payload;
  stream >> payload.hash_method;
  stream >> payload.input_paths;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_benchmark_payload(
    QDataStream& stream,
    const std::optional<TaskIpcBenchmarkPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->method_value;
  stream << payload->dictionary_size;
  stream << payload->thread_count;
  stream << payload->operands;
}

bool read_benchmark_payload(
    QDataStream& stream,
    std::optional<TaskIpcBenchmarkPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcBenchmarkPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcBenchmarkPayload payload;
  stream >> payload.method_value;
  stream >> payload.dictionary_size;
  stream >> payload.thread_count;
  stream >> payload.operands;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_open_payload(QDataStream& stream,
                        const std::optional<TaskIpcOpenPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->archive_path;
  stream << payload->archive_type;
  stream << payload->nested_archive_entries;
  stream << payload->entry_path;
}

bool read_open_payload(QDataStream& stream,
                       std::optional<TaskIpcOpenPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcOpenPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcOpenPayload payload;
  stream >> payload.archive_path;
  stream >> payload.archive_type;
  stream >> payload.nested_archive_entries;
  stream >> payload.entry_path;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

void write_cli_payload(QDataStream& stream,
                       const std::optional<TaskIpcCliPayload>& payload) {
  write_optional_presence(stream, payload);
  if (!payload.has_value()) {
    return;
  }
  stream << payload->argv;
  stream << payload->working_dir;
}

bool read_cli_payload(QDataStream& stream,
                      std::optional<TaskIpcCliPayload>* out_payload) {
  if (out_payload == nullptr) {
    return false;
  }
  bool has_value = false;
  if (!read_optional_presence<TaskIpcCliPayload>(stream, &has_value)) {
    return false;
  }
  out_payload->reset();
  if (!has_value) {
    return true;
  }
  TaskIpcCliPayload payload;
  stream >> payload.argv;
  stream >> payload.working_dir;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  *out_payload = std::move(payload);
  return true;
}

}  // namespace

QByteArray serialize_task_payload(const TaskIpcPayload& payload,
                                  QString* error_message) {
  QByteArray body;
  QDataStream body_stream(&body, QIODevice::WriteOnly);
  body_stream.setByteOrder(QDataStream::LittleEndian);
  body_stream << static_cast<quint32>(payload.command);
  body_stream << payload.show_dialog;
  body_stream << payload.refresh_after_finish;
  body_stream << payload.complete_on_claim;
  body_stream << payload.caption;
  write_add_payload(body_stream, payload.add);
  write_extract_payload(body_stream, payload.extract);
  write_test_payload(body_stream, payload.test);
  write_hash_payload(body_stream, payload.hash);
  write_benchmark_payload(body_stream, payload.benchmark);
  write_open_payload(body_stream, payload.open);
  write_archive_export_payload(body_stream, payload.archive_export);
  write_cli_payload(body_stream, payload.cli);
  if (body_stream.status() != QDataStream::Ok) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to serialize task IPC task payload.");
    }
    return QByteArray();
  }

  const QByteArray checksum =
      QCryptographicHash::hash(body, QCryptographicHash::Sha256);
  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream << static_cast<quint32>(kTaskIpcPayloadMagic);
  stream << static_cast<quint16>(kTaskIpcPayloadVersion);
  stream << static_cast<quint32>(body.size());
  stream << checksum;
  if (!body.isEmpty()) {
    const int bytes_written =
        stream.writeRawData(body.constData(), body.size());
    if (bytes_written != body.size()) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Failed to serialize task IPC task payload body.");
      }
      return QByteArray();
    }
  }
  if (stream.status() != QDataStream::Ok) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to serialize task IPC task payload.");
    }
    return QByteArray();
  }
  return encoded;
}

bool deserialize_task_payload(const QByteArray& encoded,
                              TaskIpcPayload* out_payload,
                              QString* error_message) {
  if (out_payload == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing task IPC task payload output.");
    }
    return false;
  }

  QDataStream stream(encoded);
  stream.setByteOrder(QDataStream::LittleEndian);
  quint32 magic = 0;
  quint16 version = 0;
  quint32 body_size = 0;
  QByteArray stored_checksum;
  stream >> magic;
  stream >> version;
  stream >> body_size;
  stream >> stored_checksum;

  if (stream.status() != QDataStream::Ok) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC task payload is corrupted.");
    }
    return false;
  }
  if (magic != kTaskIpcPayloadMagic || version != kTaskIpcPayloadVersion) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Unsupported task IPC task payload format.");
    }
    return false;
  }
  if (body_size == 0 || body_size > static_cast<quint32>(encoded.size())) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload body size is invalid.");
    }
    return false;
  }

  QByteArray body(static_cast<int>(body_size), Qt::Uninitialized);
  const int bytes_read = stream.readRawData(body.data(), body.size());
  if (bytes_read != body.size()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload body is truncated.");
    }
    return false;
  }
  if (stream.status() != QDataStream::Ok) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload body is corrupted.");
    }
    return false;
  }

  const QByteArray expected_checksum =
      QCryptographicHash::hash(body, QCryptographicHash::Sha256);
  if (stored_checksum != expected_checksum) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload checksum mismatch.");
    }
    return false;
  }

  QDataStream body_stream(body);
  body_stream.setByteOrder(QDataStream::LittleEndian);
  *out_payload = TaskIpcPayload{};
  quint32 command = 0;
  body_stream >> command;
  body_stream >> out_payload->show_dialog;
  body_stream >> out_payload->refresh_after_finish;
  body_stream >> out_payload->complete_on_claim;
  body_stream >> out_payload->caption;
  if (!read_add_payload(body_stream, &out_payload->add) ||
      !read_extract_payload(body_stream, &out_payload->extract) ||
      !read_test_payload(body_stream, &out_payload->test) ||
      !read_hash_payload(body_stream, &out_payload->hash) ||
      !read_benchmark_payload(body_stream, &out_payload->benchmark) ||
      !read_open_payload(body_stream, &out_payload->open) ||
      !read_archive_export_payload(body_stream, &out_payload->archive_export) ||
      !read_cli_payload(body_stream, &out_payload->cli)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload command payload is corrupted.");
    }
    return false;
  }
  if (body_stream.status() != QDataStream::Ok) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC task payload fields are corrupted.");
    }
    return false;
  }

  out_payload->command = static_cast<TaskIpcCommandKind>(command);
  return true;
}

bool write_request_payload_to_slot(QSharedMemory* request_pool_memory,
                                   int slot_index, const QByteArray& payload,
                                   QString* error_message) {
  if (request_pool_memory == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool shared memory is null.");
    }
    return false;
  }
  if (slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool slot index is invalid.");
    }
    return false;
  }
  if (payload.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC request payload is empty.");
    }
    return false;
  }
  if (payload.size() > kTaskIpcRequestPoolSlotSize) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request payload exceeds fixed request-pool slot capacity.");
    }
    return false;
  }
  if (request_pool_memory->size() < kTaskIpcRequestPoolSharedMemorySize) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request-pool shared memory size is invalid.");
    }
    return false;
  }
  TaskIpcRequestPoolHeaderRaw* raw = request_pool_raw(request_pool_memory);
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool payload pointer is null.");
    }
    return false;
  }
  char* slot_bytes = request_pool_slot_payload(raw, slot_index);
  if (slot_bytes == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool slot payload pointer is null.");
    }
    return false;
  }
  std::memset(slot_bytes, 0, static_cast<size_t>(kTaskIpcRequestPoolSlotSize));
  std::memcpy(slot_bytes, payload.constData(),
              static_cast<size_t>(payload.size()));
  return true;
}

bool read_request_payload_from_slot(QSharedMemory* request_pool_memory,
                                    int slot_index, quint32 payload_size,
                                    TaskIpcPayload* out_payload,
                                    QString* error_message) {
  if (request_pool_memory == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool shared memory is null.");
    }
    return false;
  }
  if (slot_index < 0 || slot_index >= kTaskIpcSlotCount) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool slot index is invalid.");
    }
    return false;
  }
  if (payload_size == 0U ||
      payload_size > static_cast<quint32>(kTaskIpcRequestPoolSlotSize)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request payload size is invalid.");
    }
    return false;
  }
  if (request_pool_memory->size() < kTaskIpcRequestPoolSharedMemorySize) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Task IPC request-pool shared memory size is invalid.");
    }
    return false;
  }

  const TaskIpcRequestPoolHeaderRaw* raw =
      request_pool_raw(request_pool_memory);
  if (raw == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool payload pointer is null.");
    }
    return false;
  }
  const char* slot_bytes = request_pool_slot_payload(raw, slot_index);
  if (slot_bytes == nullptr) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Task IPC request-pool slot payload pointer is null.");
    }
    return false;
  }
  const QByteArray payload(slot_bytes, static_cast<int>(payload_size));
  return deserialize_task_payload(payload, out_payload, error_message);
}

}  // namespace z7::task_ipc_runtime::task_ipc_internal
