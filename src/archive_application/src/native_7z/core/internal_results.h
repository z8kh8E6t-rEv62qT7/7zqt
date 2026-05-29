// src/archive_application/src/native_7z/core/internal_results.h
// Role: Shared result/status templates for native backend operations.

#pragma once

#include "core/internal_base.h"

namespace z7::app {

struct UpdateOperationStatus {
  int hresult = E_FAIL;
  bool totals_known = false;
  uint64_t total_bytes = 0;
  uint64_t completed_bytes = 0;
  uint64_t total_files = 0;
  uint64_t completed_files = 0;
  uint64_t error_count = 0;
  std::string current_path;
  std::optional<ProgressRatioInfo> ratio_info;
  bool password_requested = false;
  bool wrong_password = false;
  std::string diagnostic;
};

UpdateOperationStatus run_update_archive_shared(
    CCodecs* codecs,
    CObjectVector<COpenType>& types,
    const std::string& archive_path,
    NWildcard::CCensor& censor,
    CUpdateOptions& options,
    CUpdateErrorInfo& error_info,
    NativeUpdateOperationCallback& callback);

template <typename TResult>
TResult make_operation_failure(
    ArchiveError error,
    NativeTerminationReason termination_reason = NativeTerminationReason::kCompleted) {
  TResult result;
  result.ok = false;
  result.error = std::move(error);
  result.native_exit_code = result.error.native_exit_code;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = termination_reason;
  result.summary = describe_archive_error(result.error);
  return result;
}

template <typename TResult>
TResult make_operation_failure(
    ArchiveErrorDomain domain,
    std::string message,
    int native_exit_code,
    NativeTerminationReason termination_reason = NativeTerminationReason::kCompleted) {
  return make_operation_failure<TResult>(
      make_archive_error(domain, std::move(message), native_exit_code),
      termination_reason);
}

template <typename TResult>
TResult make_operation_failure_from_hresult(int hr) {
  ArchiveError error = map_hresult_to_archive_error(hr);
  const NativeTerminationReason termination_reason =
      error.domain == ArchiveErrorDomain::kCanceled
          ? NativeTerminationReason::kCanceled
          : NativeTerminationReason::kCompleted;
  return make_operation_failure<TResult>(std::move(error), termination_reason);
}

template <typename TResult, typename Handler>
TResult run_with_loaded_codecs(Handler&& handler) {
  CCodecs codecs;
  const HRESULT load_res = load_codecs_shared(codecs);
  if (load_res != S_OK) {
    return make_operation_failure_from_hresult<TResult>(load_res);
  }
  return handler(codecs);
}

template <typename TResult>
TResult make_operation_partial_success(
    std::string message = "Operation completed with warnings") {
  return make_operation_failure<TResult>(ArchiveErrorDomain::kPartialSuccess,
                                         std::move(message),
                                         1);
}

template <typename TResult>
TResult make_operation_canceled() {
  return make_operation_failure<TResult>(ArchiveErrorDomain::kCanceled,
                                         "Operation canceled",
                                         255,
                                         NativeTerminationReason::kCanceled);
}

template <typename TResult>
TResult make_operation_success(std::string summary) {
  TResult result;
  result.ok = true;
  result.native_exit_code = 0;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(ArchiveErrorDomain::kNone, {}, 0);
  result.summary = std::move(summary);
  return result;
}

template <typename TResult>
void attach_error_message(TResult* result, std::string message) {
  if (result == nullptr || message.empty()) {
    return;
  }
  result->error.message = std::move(message);
  result->summary = describe_archive_error(result->error);
}

template <typename TResult>
TResult finalize_update_operation_result(
    const ArchiveBackendHooks& callbacks,
    const std::atomic<bool>& cancel_requested,
    const UpdateOperationStatus& status,
    std::string success_summary = "Everything is Ok") {
  emit_progress_event(callbacks,
                      OperationStage::kRunning,
                      100,
                      status.totals_known,
                      status.total_bytes,
                      status.completed_bytes,
                      status.total_files,
                      status.completed_files,
                      status.error_count,
                      status.current_path,
                      {},
                      status.ratio_info);

  if (cancel_requested.load()) {
    return make_operation_canceled<TResult>();
  }

  if (status.password_requested || status.wrong_password) {
    return make_operation_failure<TResult>(ArchiveErrorDomain::kPassword,
                                           "Password required or incorrect",
                                           2);
  }

  if (status.hresult != S_OK) {
    if (status.hresult == S_FALSE && status.error_count != 0) {
      if (status.diagnostic.empty()) {
        return make_operation_partial_success<TResult>();
      }
      return make_operation_partial_success<TResult>(status.diagnostic);
    }

    TResult failure = make_operation_failure_from_hresult<TResult>(status.hresult);
    attach_error_message(&failure, status.diagnostic);
    return failure;
  }

  if (status.error_count != 0) {
    if (status.diagnostic.empty()) {
      return make_operation_partial_success<TResult>();
    }
    return make_operation_partial_success<TResult>(status.diagnostic);
  }

  return make_operation_success<TResult>(std::move(success_summary));
}

template <typename TResult, typename OnFailure, typename OnSuccess>
TResult finalize_hresult_operation_result(
    const std::atomic<bool>& cancel_requested,
    int hresult,
    std::string success_summary,
    OnFailure&& on_failure,
    OnSuccess&& on_success) {
  if (cancel_requested.load() || hresult == E_ABORT) {
    return make_operation_canceled<TResult>();
  }
  if (hresult != S_OK) {
    TResult failure = make_operation_failure_from_hresult<TResult>(hresult);
    on_failure(failure);
    return failure;
  }
  TResult success = make_operation_success<TResult>(std::move(success_summary));
  on_success(success);
  return success;
}

struct ExtractRollbackEntry {
  fs::path output_path;
  fs::path destination_path;
  fs::path backup_path;
  bool had_original = false;
  bool preserve_backup_on_commit = false;
  bool is_directory = false;
};

struct ExtractInvocationStatus {
  int hresult = E_FAIL;
  bool totals_known = false;
  uint64_t total_bytes = 0;
  uint64_t completed_bytes = 0;
  uint64_t completed_files = 0;
  uint64_t error_count = 0;
  std::string current_path;
  std::optional<ProgressRatioInfo> ratio_info;
  bool password_requested = false;
  bool wrong_password = false;
  bool has_io_error = false;
  std::string io_error_message;
  std::string diagnostic;
  std::vector<ExtractMaterializedEntry> materialized_entries;
  std::vector<ExtractRollbackEntry> rollback_entries;
  bool budget_triggered = false;
  std::string budget_trigger_reason;
  BudgetExceededAction budget_policy = BudgetExceededAction::kFailAndRollback;
};

template <typename CallbackT, typename = void>
struct HasExtractIoErrorMethods : std::false_type {};

template <typename CallbackT>
struct HasExtractIoErrorMethods<
    CallbackT,
    std::void_t<decltype(std::declval<CallbackT*>()->has_io_error()),
                decltype(std::declval<CallbackT*>()->io_error_message())>> : std::true_type {};

template <typename CallbackT, typename = void>
struct HasExtractDiagnosticMethod : std::false_type {};

template <typename CallbackT>
struct HasExtractDiagnosticMethod<
    CallbackT,
    std::void_t<decltype(std::declval<CallbackT*>()->diagnostic_message())>>
    : std::true_type {};

template <typename CallbackT, typename = void>
struct HasBudgetTriggered : std::false_type {};

template <typename CallbackT>
struct HasBudgetTriggered<
    CallbackT,
    std::void_t<decltype(std::declval<CallbackT*>()->budget_triggered())>>
    : std::true_type {};

template <typename CallbackT, typename = void>
struct HasTakeMaterializedEntries : std::false_type {};

template <typename CallbackT>
struct HasTakeMaterializedEntries<
    CallbackT,
    std::void_t<decltype(std::declval<CallbackT*>()->take_materialized_entries())>>
    : std::true_type {};

template <typename CallbackT, typename = void>
struct HasTakeRollbackEntries : std::false_type {};

template <typename CallbackT>
struct HasTakeRollbackEntries<
    CallbackT,
    std::void_t<decltype(std::declval<CallbackT*>()->take_rollback_entries())>>
    : std::true_type {};

template <typename CallbackT>
ExtractInvocationStatus invoke_archive_extract_with_callback(
    IInArchive* archive,
    const UInt32* indices,
    UInt32 num_indices,
    bool test_mode,
    CallbackT* callback) {
  ExtractInvocationStatus status;
  status.hresult = archive->Extract(indices, num_indices, BoolToInt(test_mode), callback);
  status.totals_known = callback->totals_known();
  status.total_bytes = callback->total_bytes();
  status.completed_bytes = callback->completed_bytes();
  status.completed_files = callback->completed_files();
  status.error_count = callback->error_count();
  status.current_path = callback->current_path();
  status.ratio_info = callback->ratio_info();
  status.password_requested = callback->password_requested();
  status.wrong_password = callback->wrong_password();
  if constexpr (HasExtractIoErrorMethods<CallbackT>::value) {
    status.has_io_error = callback->has_io_error();
    status.io_error_message = callback->io_error_message();
  }
  if constexpr (HasExtractDiagnosticMethod<CallbackT>::value) {
    status.diagnostic = callback->diagnostic_message();
  }
  if constexpr (HasTakeMaterializedEntries<CallbackT>::value) {
    status.materialized_entries = callback->take_materialized_entries();
  }
  if constexpr (HasTakeRollbackEntries<CallbackT>::value) {
    status.rollback_entries = callback->take_rollback_entries();
  }
  if constexpr (HasBudgetTriggered<CallbackT>::value) {
    status.budget_triggered = callback->budget_triggered();
    if (status.budget_triggered) {
      status.budget_trigger_reason = callback->budget_trigger_reason();
      status.budget_policy = callback->budget_policy();
    }
  }
  callback->Release();
  return status;
}

template <typename TResult, typename OnPartial, typename OnSuccess>
TResult finalize_extract_operation_result(
    const ArchiveBackendHooks& hooks,
    const std::atomic<bool>& cancel_requested,
    uint64_t total_files,
    const ExtractInvocationStatus& status,
    OnPartial&& on_partial,
    OnSuccess&& on_success,
    std::string password_error_summary = "Password required or incorrect") {
  emit_progress_event(hooks,
                      OperationStage::kRunning,
                      100,
                      status.totals_known,
                      status.total_bytes,
                      status.completed_bytes,
                      total_files,
                      status.completed_files,
                      status.error_count,
                      status.current_path,
                      {},
                      status.ratio_info);

  if (cancel_requested.load()) {
    return make_operation_canceled<TResult>();
  }

  if (status.password_requested || status.wrong_password) {
    return make_operation_failure<TResult>(ArchiveErrorDomain::kPassword,
                                           std::move(password_error_summary),
                                           2);
  }

  if (status.hresult != S_OK) {
    if (status.has_io_error) {
      return make_operation_failure<TResult>(ArchiveErrorDomain::kIo,
                                             status.io_error_message.empty()
                                                 ? std::string("I/O error")
                                                 : status.io_error_message,
                                             2);
    }
    return make_operation_failure_from_hresult<TResult>(status.hresult);
  }

  if (status.error_count != 0) {
    return on_partial(status);
  }
  return on_success(status);
}

template <typename TResult, typename Paths, typename ActionFn, typename OnSuccess>
TResult run_filesystem_path_batch(const Paths& paths,
                                  const ArchiveBackendHooks& hooks,
                                  std::atomic<bool>& cancel_requested,
                                  ActionFn&& action,
                                  std::string success_summary,
                                  OnSuccess&& on_success) {
  const uint64_t total = static_cast<uint64_t>(paths.size());
  uint64_t completed = 0;
  for (const std::string& path : paths) {
    if (cancel_requested.load()) {
      return make_operation_canceled<TResult>();
    }
    if (std::optional<ArchiveError> error = action(path); error.has_value()) {
      return make_operation_failure<TResult>(std::move(*error));
    }

    ++completed;
    emit_progress_event(hooks,
                        OperationStage::kRunning,
                        total == 0 ? -1 : static_cast<int>((completed * 100) / total),
                        true,
                        0,
                        0,
                        total,
                        completed,
                        0,
                        path,
                        {});
  }

  TResult result = make_operation_success<TResult>(std::move(success_summary));
  on_success(result, completed);
  return result;
}

template <typename TResult, typename ActionFn, typename OnSuccess>
TResult run_filesystem_single_step(const ArchiveBackendHooks& hooks,
                                   std::atomic<bool>& cancel_requested,
                                   std::string progress_path,
                                   ActionFn&& action,
                                   std::string success_summary,
                                   OnSuccess&& on_success) {
  if (cancel_requested.load()) {
    return make_operation_canceled<TResult>();
  }

  emit_progress_event(hooks,
                      OperationStage::kRunning,
                      -1,
                      false,
                      0,
                      0,
                      1,
                      0,
                      0,
                      progress_path,
                      {});

  if (std::optional<ArchiveError> error = action(); error.has_value()) {
    return make_operation_failure<TResult>(std::move(*error));
  }
  if (cancel_requested.load()) {
    return make_operation_canceled<TResult>();
  }

  TResult result = make_operation_success<TResult>(std::move(success_summary));
  on_success(result);
  emit_progress_event(hooks,
                      OperationStage::kRunning,
                      100,
                      false,
                      0,
                      0,
                      1,
                      1,
                      0,
                      progress_path,
                      {});
  return result;
}

std::string normalize_archive_virtual_directory(std::string directory);
std::vector<std::string> split_archive_virtual_directory(
    const std::string& directory);
bool archive_virtual_path_is_safe_for_materialization(
    const std::string& normalized_path);

bool archive_get_prop_uint64(IInArchive* archive,
                             UInt32 index,
                             PROPID prop_id,
                             uint64_t& value);
bool archive_get_prop_uint32(IInArchive* archive,
                             UInt32 index,
                             PROPID prop_id,
                             uint32_t& value);
bool archive_get_prop_bool(IInArchive* archive,
                           UInt32 index,
                           PROPID prop_id,
                           bool& value);
bool archive_get_prop_time_msecs_utc(IInArchive* archive,
                                     UInt32 index,
                                     PROPID prop_id,
                                     int64_t& value);
std::string archive_get_prop_text(IInArchive* archive,
                                  UInt32 index,
                                  PROPID prop_id);

void fill_archive_list_entry_details(IInArchive* archive,
                                     UInt32 arc_index,
                                     ArchiveListEntry& entry);
void fill_proxy_dir_stats(const CProxyDir& dir, ArchiveListEntry& entry);
void fill_proxy_dir2_stats(const CProxyDir2& dir, ArchiveListEntry& entry);
std::string test_operation_result_message(Int32 op_res);


}  // namespace z7::app
