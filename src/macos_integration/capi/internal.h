#pragma once

#include "macos_integration_c_api.h"

#include <QHash>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "archive_session.h"
#include "macos_integration_config.h"
#include "shell_integration_menu.h"
#include "archive_string_codec_qt.h"

struct AsyncTaskState {
  mutable std::mutex archive_session_mutex;
  z7::app::ArchiveSession archive_session;
  std::shared_ptr<z7::app::IArchiveDelegate> delegate;
  std::mutex completion_mutex;
  std::condition_variable completion_cv;
  std::atomic<bool> cancel_requested = false;
  std::atomic<bool> completed = false;
  std::atomic<bool> callback_dispatched = false;
  std::atomic<bool> password_prompt_canceled = false;
  std::atomic<bool> password_prompt_missing_callback = false;
};

struct NestedSessionChain {
  std::vector<z7::app::ArchiveSessionToken> tokens;
  std::atomic<uint32_t> active_users{0};
  std::atomic<bool> cached{false};
  std::atomic<bool> closed{false};
  std::chrono::steady_clock::time_point last_used{};
};

struct z7_mi_session_state;

struct PromptSlot {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool canceled = false;
  std::string password;
  std::atomic<int> waiter_count{0};
  std::string reason_key;
};

struct PromptDispatchEntry {
  std::mutex mutex;
  std::condition_variable cv;
  bool dispatched = false;
  bool canceled = false;
  std::shared_ptr<PromptSlot> slot;
  QString archive_path_abs;
  QStringList nested_chain;
  QString reason_key;
};

struct z7_mi_password_prompt {
  std::shared_ptr<PromptSlot> slot;
  std::shared_ptr<PromptDispatchEntry> dispatch;
  std::weak_ptr<z7_mi_session_state> state;
};

struct z7_mi_session_state {
  QHash<QString, std::string> password_cache;
  QHash<QString, std::shared_ptr<NestedSessionChain>> nested_session_cache;
  QHash<QString, std::shared_ptr<PromptSlot>> pending_prompts;
  std::vector<std::weak_ptr<PromptSlot>> orphan_prompt_slots;
  std::shared_ptr<PromptDispatchEntry> active_prompt_dispatch;
  std::deque<std::shared_ptr<PromptDispatchEntry>> queued_prompt_dispatches;
  z7_mi_password_prompt_callback_t password_prompt_callback = nullptr;
  void* password_prompt_user_data = nullptr;
  std::mutex mutex;
  uint64_t next_task_id = 1;
  std::unordered_map<uint64_t, std::shared_ptr<AsyncTaskState>> in_flight_tasks;
};

struct z7_mi_session {
  std::shared_ptr<z7_mi_session_state> state;
};

struct z7_mi_task {
  std::weak_ptr<z7_mi_session_state> state;
  uint64_t task_id = 0;
};

namespace z7::macos_integration::capi_internal {

using z7::macos_integration::MacOSIntegrationConfigSnapshot;
using z7::shell_integration::ShellIntegrationConfig;

inline constexpr uint64_t kQuicklookDirectoryMaxFiles = 10000;
inline constexpr uint64_t kQuicklookDirectoryMaxBytes = 2ULL * 1024 * 1024 * 1024;
inline constexpr uint64_t kQuicklookBatchExportDirectoryMaxFiles = 1000;
inline constexpr uint64_t kQuicklookBatchExportMaxBytes =
    1ULL * 1024 * 1024 * 1024;

void ensure_qt_core_app();
QString to_qstring(const char* value);
char* duplicate_c_string(const QString& value);
void free_c_string(const char*& value);
QStringList string_list_from_utf8(const char* const* values, size_t count);
QString normalize_virtual_entry_path(const QString& entry);
QString join_virtual_dir_with_name(const QString& virtual_dir,
                                   const QString& name);
QString bundled_program_path_from_process_dir(const QString& process_dir_path,
                                              const QString& program_name);
uint64_t register_in_flight_task(const std::shared_ptr<z7_mi_session_state>& state,
                                 std::shared_ptr<AsyncTaskState> task);
void unregister_in_flight_task(const std::weak_ptr<z7_mi_session_state>& weak_state,
                               uint64_t task_id);
void unregister_in_flight_task(const std::shared_ptr<z7_mi_session_state>& state,
                               uint64_t task_id);
std::shared_ptr<AsyncTaskState> lookup_in_flight_task(
    const std::shared_ptr<z7_mi_session_state>& state,
    uint64_t task_id);
void set_active_archive_session(const std::shared_ptr<AsyncTaskState>& task_state,
                                const z7::app::ArchiveSession& session);
void clear_active_archive_session(const std::shared_ptr<AsyncTaskState>& task_state);
void cancel_active_archive_session(const std::shared_ptr<AsyncTaskState>& task_state);
void mark_task_completed(const std::shared_ptr<AsyncTaskState>& task_state);
void wait_for_task_completion(const std::shared_ptr<AsyncTaskState>& task_state);
bool report_once(std::shared_ptr<AsyncTaskState> task_state);
bool should_report_canceled(const z7::app::OperationOutcome& outcome,
                            bool cancel_requested);
QString canceled_message(const z7::app::OperationOutcome& outcome);
QString fallback_archive_error_summary(const z7::app::OperationOutcome& outcome);
QString fallback_archive_error_summary(const z7::app::ArchiveError& error,
                                       const QString& summary);
void dispatch_result_closure(std::function<void()> closure);

template <typename TResult>
TResult* allocate_result() {
  return static_cast<TResult*>(std::calloc(1, sizeof(TResult)));
}

template <typename TResult, typename TCallback>
void dispatch_result(TResult* result, TCallback callback, void* user_data) {
  if (!callback) {
    return;
  }
  dispatch_result_closure([result, callback, user_data]() {
    callback(result, user_data);
  });
}

QString infer_archive_format(const QString& archive_path,
                             const QString& type_hint);
QStringList normalize_nested_archive_entries(const char* const* values,
                                            size_t count,
                                            QString* error_message);
QString nested_session_cache_key(const QString& archive_path,
                                 const QString& effective_archive_type,
                                 const QStringList& nested_archive_entries);
bool is_archive_like_by_name(const QString& name);
QString format_nested_error_message(size_t level, const QString& reason);
z7_mi_status_t open_nested_session_chain(
    const std::shared_ptr<z7_mi_session_state>& state,
    const QString& archive_path,
    const QString& archive_type_hint,
    const QStringList& nested_archive_entries,
    const std::shared_ptr<AsyncTaskState>& task_state,
    std::shared_ptr<NestedSessionChain>* out_chain,
    QString* error_message);
z7::app::OperationOutcome run_archive_request_sync(
    const z7::app::ArchiveRequest& request,
    const std::shared_ptr<AsyncTaskState>& task_state,
    std::shared_ptr<z7::app::IArchiveDelegate> delegate = {});
void release_nested_session_chain(const std::shared_ptr<z7_mi_session_state>& state,
                                  const std::shared_ptr<NestedSessionChain>& chain);
void close_nested_session_chain(const std::shared_ptr<NestedSessionChain>& chain);
std::vector<std::shared_ptr<NestedSessionChain>> take_nested_session_cache_for_destroy(
    const std::shared_ptr<z7_mi_session_state>& state);
ShellIntegrationConfig runtime_config_from_snapshot(
    const MacOSIntegrationConfigSnapshot& snapshot);
bool ensure_portable_settings(z7_mi_session_t* session,
                              QString* error_message);
void init_menu_plan_error(z7_mi_menu_plan_t* out_plan,
                          z7_mi_status_t status,
                          const QString& error);
void init_action_result_error(z7_mi_action_result_t* out_result,
                              z7_mi_status_t status,
                              const QString& error,
                              const QString& action_id);
void init_quicklook_list_error(z7_mi_quicklook_list_result_t* out_result,
                               z7_mi_status_t status,
                               const QString& error);
void init_quicklook_batch_export_error(
    z7_mi_quicklook_batch_export_result_t* out_result,
    z7_mi_status_t status,
    const QString& error,
    size_t completed_item_count,
    size_t total_item_count,
    int64_t failed_item_index,
    const QString& failed_entry_path,
    const QString& failed_destination_path);
void finish_prompt_slot(const std::shared_ptr<PromptSlot>& slot,
                        bool canceled,
                        std::string password = {});
void cancel_all_pending_prompts(const std::shared_ptr<z7_mi_session_state>& state);
void clear_password_cache(const std::shared_ptr<z7_mi_session_state>& state);
void queue_password_prompt_dispatch(
    const std::shared_ptr<z7_mi_session_state>& state,
    const std::shared_ptr<PromptDispatchEntry>& dispatch);
void advance_password_prompt_dispatch(
    const std::shared_ptr<z7_mi_session_state>& state,
    const std::shared_ptr<PromptDispatchEntry>& completed_dispatch);

}  // namespace z7::macos_integration::capi_internal
