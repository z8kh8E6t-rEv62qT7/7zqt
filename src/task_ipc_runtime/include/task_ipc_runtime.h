#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QSharedMemory;

namespace z7::task_ipc_runtime {

enum class TaskIpcExtractPathRemapMatchKind : quint32 {
  kRequestRoot = 0,
  kExactArchivePath = 1,
  kArchivePrefix = 2
};

struct TaskIpcExtractPathRemap {
  TaskIpcExtractPathRemapMatchKind match_kind =
      TaskIpcExtractPathRemapMatchKind::kRequestRoot;
  QString source_path;
  QString destination_path;
};

struct TaskIpcAddPayload {
  QString archive_path;
  QString archive_type;
  QString update_mode;
  QString raw_update_switch;
  QStringList raw_update_switches;
  QString path_mode;
  bool create_sfx = false;
  bool share_for_write = false;
  bool delete_after_compressing = false;
  bool send_by_email = false;
  bool send_by_email_remove_after = false;
  QString send_by_email_address;
  QString compression_level;
  QString method_value;
  QString dictionary_size;
  QString word_size;
  QString solid_block_size;
  QString thread_count;
  QString volume_size;
  QString password;
  bool encrypt_headers_defined = false;
  bool encrypt_headers = false;
  QString encryption_method;
  QStringList extra_parameters;
  QStringList input_paths;
};

struct TaskIpcExtractPayload {
  QString output_dir;
  bool split_dest_enabled = false;
  QString split_dest_name;
  QString overwrite_switch;
  QString archive_type;
  bool eliminate_root_duplication = false;
  QVector<TaskIpcExtractPathRemap> path_remaps;
  bool restore_file_security = false;
  QString zone_id_mode;
  QString password;
  QStringList archive_inputs;
};

struct TaskIpcArchiveExportPayload {
  QString root_archive_path;
  QString root_archive_type;
  QStringList nested_archive_entries;
  QStringList archive_entry_paths;
  QString output_dir;
  QString overwrite_mode;
  QString path_mode;
  bool eliminate_root_duplication = false;
  QVector<TaskIpcExtractPathRemap> path_remaps;
  bool restore_file_security = false;
  QString zone_id_mode;
  QString password;
};

struct TaskIpcTestPayload {
  QStringList archive_inputs;
};

struct TaskIpcHashPayload {
  QString hash_method;
  QStringList input_paths;
};

struct TaskIpcBenchmarkPayload {
  QString method_value;
  QString dictionary_size;
  QString thread_count;
  QStringList operands;
};

struct TaskIpcOpenPayload {
  QString archive_path;
  QString archive_type;
  QStringList nested_archive_entries;
  QString entry_path;
};

struct TaskIpcCliPayload {
  // The worker reparses these original argv tokens with CArcCmdLineParser.
  // Keeping selector switches and @listfile references intact preserves the
  // original CCensor wildcard/recursive semantics across the IPC boundary.
  QStringList argv;
  QString working_dir;
};

using TaskIpcEventNotifier = std::function<void(const QString&)>;
using TaskIpcCancelNotifier = std::function<void()>;

enum class TaskIpcCommandKind : quint32 {
  kNone = 0,
  kAdd = 1,
  kExtract = 2,
  kTest = 3,
  kHash = 4,
  kBenchmark = 5,
  kOpen = 6,
  kArchiveExport = 7,
  kCli = 8
};

struct TaskIpcPayload {
  TaskIpcCommandKind command = TaskIpcCommandKind::kNone;
  bool show_dialog = false;
  bool refresh_after_finish = true;
  bool complete_on_claim = false;
  QString caption;
  std::optional<TaskIpcAddPayload> add;
  std::optional<TaskIpcExtractPayload> extract;
  std::optional<TaskIpcTestPayload> test;
  std::optional<TaskIpcHashPayload> hash;
  std::optional<TaskIpcBenchmarkPayload> benchmark;
  std::optional<TaskIpcOpenPayload> open;
  std::optional<TaskIpcArchiveExportPayload> archive_export;
  std::optional<TaskIpcCliPayload> cli;
};

enum class TaskIpcSlotState : quint32 {
  kEmpty = 0,
  kDispatched = 1,
  kClaimed = 2,
  kCompleted = 3
};

enum class TaskIpcEventKind : quint32 {
  kNone = 0,
  kDispatched = 1,
  kClaimed = 2,
  kCompleted = 3
};

struct TaskIpcDispatchResult {
  quint64 session_id = 0;
  quint32 generation = 0;
  qint64 worker_pid = 0;
};

struct TaskIpcManagedProcessOptions {
  bool forward_stdin = false;
  bool forward_stdout = false;
};

struct TaskIpcClaimedTask {
  int slot_index = -1;
  quint64 session_id = 0;
  quint32 generation = 0;
  QString ipc_shm_name;
  QString ipc_sem_name;
  QString owner_instance_id;
  qint64 launcher_pid = 0;
  qint64 worker_pid = 0;
  TaskIpcPayload payload;
};

struct TaskIpcEvent {
  quint64 session_id = 0;
  quint32 generation = 0;
  TaskIpcEventKind event_kind = TaskIpcEventKind::kNone;
  quint32 event_sequence = 0;
  QString owner_instance_id;
  qint64 worker_pid = 0;
  int result_code = 0;
  bool refresh_after_finish = true;
  QString summary;
  TaskIpcPayload payload;
};

QString task_ipc_bootstrap_key();
QString task_ipc_request_pool_key();
QString ensure_task_ipc_owner_instance_id();
bool ensure_task_ipc_bootstrap_ready(QString* error_message);
void set_task_ipc_worker_endpoint(const QString& shm_name,
                                  const QString& sem_name);
bool set_task_ipc_event_notifier(
    const QString& owner_instance_id,
    TaskIpcEventNotifier notifier,
    QString* error_message);
bool clear_task_ipc_event_notifier(const QString& owner_instance_id,
                                   QString* error_message);
bool set_task_ipc_cancel_notifier(const TaskIpcClaimedTask& task,
                                  TaskIpcCancelNotifier notifier,
                                  QString* error_message);

bool dispatch_task_ipc_task(const QString& worker_program,
                            const QString& working_dir,
                            const QString& owner_instance_id,
                            const TaskIpcPayload& payload,
                            TaskIpcDispatchResult* out_result,
                            QString* error_message);

bool dispatch_task_ipc_task_managed_process(
    const QString& worker_program,
    const QString& working_dir,
    const QString& owner_instance_id,
    const TaskIpcPayload& payload,
    const TaskIpcManagedProcessOptions& process_options,
    TaskIpcDispatchResult* out_result,
    QProcess** out_process,
    QString* error_message);

bool request_task_ipc_cancel(quint64 session_id,
                             quint32 generation,
                             QString* error_message);

bool claim_task_ipc_task_for_worker(quint64 session_id, quint32 generation,
                                    TaskIpcClaimedTask* out_task,
                                    QString* error_message);

bool query_task_ipc_cancel_requested(const TaskIpcClaimedTask& task,
                                     bool* out_canceled,
                                     QString* error_message);

bool publish_task_ipc_completion(const TaskIpcClaimedTask& task,
                                 int result_code, const QString& summary,
                                 QString* error_message);

bool publish_task_ipc_completion_minimal(const TaskIpcClaimedTask& task,
                                         int result_code,
                                         QString* error_message);

bool collect_task_ipc_events(const QString& owner_instance_id,
                             QVector<TaskIpcEvent>* out_events,
                             QString* error_message);

bool acknowledge_task_ipc_event(const TaskIpcEvent& event,
                                QString* error_message);

}  // namespace z7::task_ipc_runtime

Q_DECLARE_METATYPE(z7::task_ipc_runtime::TaskIpcPayload)
