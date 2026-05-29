#include "internal.h"
#include "password_relay_delegate.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "archive_quicklook_export.h"

namespace capi = z7::macos_integration::capi_internal;

namespace {

struct QuickLookBatchExportItem {
  QString entry_path;
  QString destination_path;
  uint64_t listed_size = 0;
  bool recursive = false;
  bool entry_is_directory = false;
};

struct QuickLookBatchExportPreparedItem {
  QuickLookBatchExportItem item;
  QString normalized_entry_path;
  QString failure_entry_path;
  QString failure_destination_path;
  uint64_t known_size = 0;
  bool size_known = false;
  uint64_t known_file_count = 0;
  bool file_count_known = false;
};

class QuickLookBatchExportDelegate final : public z7::app::IArchiveDelegate {
 public:
  QuickLookBatchExportDelegate(
      z7_mi_quicklook_batch_export_progress_callback_t progress_callback,
      void* user_data,
      size_t total_item_count)
      : progress_callback_(progress_callback),
        user_data_(user_data),
        total_item_count_(total_item_count) {}

  void set_item_context(size_t completed_item_count,
                        size_t current_item_index,
                        const QString& current_entry_path,
                        const QString& current_destination_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_item_count_ = completed_item_count;
    current_item_index_ = current_item_index;
    current_entry_path_ = current_entry_path;
    current_destination_path_ = current_destination_path;
  }

  void emit_indeterminate_progress(const QString& message) {
    if (progress_callback_ == nullptr) {
      return;
    }
    auto* progress = capi::allocate_result<z7_mi_quicklook_batch_export_progress_t>();
    if (progress == nullptr) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      progress->completed_item_count = completed_item_count_;
      progress->total_item_count = total_item_count_;
      progress->current_item_index = current_item_index_;
      progress->current_entry_path = capi::duplicate_c_string(current_entry_path_);
      progress->current_destination_path =
          capi::duplicate_c_string(current_destination_path_);
      progress->current_percent = -1;
      progress->totals_known = false;
      progress->total_bytes = 0;
      progress->completed_bytes = 0;
      progress->current_path = nullptr;
      progress->message = capi::duplicate_c_string(message);
    }
    capi::dispatch_result(progress, progress_callback_, user_data_);
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    if (progress_callback_ == nullptr) {
      return;
    }

    auto* dto = capi::allocate_result<z7_mi_quicklook_batch_export_progress_t>();
    if (dto == nullptr) {
      return;
    }

    QString current_entry_path;
    QString current_destination_path;
    size_t completed_item_count = 0;
    size_t current_item_index = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      completed_item_count = completed_item_count_;
      current_item_index = current_item_index_;
      current_entry_path = current_entry_path_;
      current_destination_path = current_destination_path_;
    }

    dto->completed_item_count = completed_item_count;
    dto->total_item_count = total_item_count_;
    dto->current_item_index = current_item_index;
    dto->current_entry_path = capi::duplicate_c_string(current_entry_path);
    dto->current_destination_path =
        capi::duplicate_c_string(current_destination_path);
    dto->current_percent = progress.percent;
    dto->totals_known = progress.totals_known;
    dto->total_bytes = progress.total_bytes;
    dto->completed_bytes = progress.completed_bytes;
    dto->current_path = capi::duplicate_c_string(
        z7::ui::archive_support::from_utf8_string(progress.current_path));
    dto->message = capi::duplicate_c_string(
        z7::ui::archive_support::from_utf8_string(progress.message));
    capi::dispatch_result(dto, progress_callback_, user_data_);
  }

 private:
  z7_mi_quicklook_batch_export_progress_callback_t progress_callback_ = nullptr;
  void* user_data_ = nullptr;
  size_t total_item_count_ = 0;
  std::mutex mutex_;
  size_t completed_item_count_ = 0;
  size_t current_item_index_ = 0;
  QString current_entry_path_;
  QString current_destination_path_;
};

QString normalize_batch_export_entry_path(const QString& entry_path,
                                          const QStringList& nested_archive_entries) {
  if (!nested_archive_entries.isEmpty()) {
    return capi::normalize_virtual_entry_path(entry_path);
  }
  return capi::normalize_virtual_entry_path(entry_path);
}

z7_mi_status_t quicklook_batch_export_status_from_outcome(
    const z7::app::OperationOutcome& outcome,
    const std::shared_ptr<AsyncTaskState>& task_state) {
  if (capi::should_report_canceled(outcome, task_state && task_state->cancel_requested.load())) {
    return Z7_MI_STATUS_CANCELED;
  }
  switch (outcome.error.domain) {
    case z7::app::ArchiveErrorDomain::kPassword:
      if (task_state && task_state->password_prompt_canceled.load()) {
        return Z7_MI_STATUS_PASSWORD_REQUIRED;
      }
      return Z7_MI_STATUS_BACKEND_ERROR;
    case z7::app::ArchiveErrorDomain::kIo:
      return Z7_MI_STATUS_IO_ERROR;
    case z7::app::ArchiveErrorDomain::kInvalidArguments:
    case z7::app::ArchiveErrorDomain::kBudgetExceeded:
      return Z7_MI_STATUS_INVALID_ARGUMENT;
    case z7::app::ArchiveErrorDomain::kBackendUnavailable:
      return Z7_MI_STATUS_INTERNAL_ERROR;
    case z7::app::ArchiveErrorDomain::kCanceled:
      return Z7_MI_STATUS_CANCELED;
    case z7::app::ArchiveErrorDomain::kNone:
    case z7::app::ArchiveErrorDomain::kPartialSuccess:
    case z7::app::ArchiveErrorDomain::kUnknown:
    case z7::app::ArchiveErrorDomain::kUnsupportedFormat:
    default:
      return Z7_MI_STATUS_BACKEND_ERROR;
  }
}

z7_mi_quicklook_batch_export_result_t* build_batch_export_success_result(
    size_t completed_item_count,
    size_t total_item_count) {
  auto* out_result =
      capi::allocate_result<z7_mi_quicklook_batch_export_result_t>();
  if (out_result == nullptr) {
    return nullptr;
  }
  out_result->ok = true;
  out_result->status = Z7_MI_STATUS_OK;
  out_result->completed_item_count = completed_item_count;
  out_result->total_item_count = total_item_count;
  out_result->failed_item_index = -1;
  return out_result;
}

void free_batch_export_progress_strings(
    z7_mi_quicklook_batch_export_progress_t* progress) {
  if (progress == nullptr) {
    return;
  }
  capi::free_c_string(progress->current_entry_path);
  capi::free_c_string(progress->current_destination_path);
  capi::free_c_string(progress->current_path);
  capi::free_c_string(progress->message);
}

void free_batch_export_result_strings(
    z7_mi_quicklook_batch_export_result_t* result) {
  if (result == nullptr) {
    return;
  }
  capi::free_c_string(result->error_message);
  capi::free_c_string(result->failed_entry_path);
  capi::free_c_string(result->failed_destination_path);
}

bool prepare_batch_export_items(
    const std::shared_ptr<z7_mi_session_state>& state,
    const QString& archive_path,
    const QString& archive_type_hint,
    const QStringList& nested_archive_entries,
    const std::shared_ptr<AsyncTaskState>& task_state,
    const std::vector<QuickLookBatchExportItem>& items,
    std::shared_ptr<NestedSessionChain>* out_chain,
    std::vector<QuickLookBatchExportPreparedItem>* out_prepared_items,
    QString* error_message,
    z7_mi_status_t* out_status,
    int64_t* failed_item_index,
    QString* failed_entry_path,
    QString* failed_destination_path) {
  if (out_chain == nullptr || out_prepared_items == nullptr || error_message == nullptr ||
      failed_item_index == nullptr || failed_entry_path == nullptr ||
      failed_destination_path == nullptr) {
    return false;
  }

  *failed_item_index = -1;
  if (out_status != nullptr) {
    *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  failed_entry_path->clear();
  failed_destination_path->clear();
  out_prepared_items->clear();
  out_chain->reset();
  error_message->clear();

  const z7_mi_status_t chain_status = capi::open_nested_session_chain(
      state,
      archive_path,
      archive_type_hint,
      nested_archive_entries,
      task_state,
      out_chain,
      error_message);
  if (chain_status != Z7_MI_STATUS_OK) {
    if (out_status != nullptr) {
      *out_status = chain_status;
    }
    return false;
  }
  if (!*out_chain || (*out_chain)->tokens.empty()) {
    *error_message = QStringLiteral("Nested session chain was empty.");
    if (out_status != nullptr) {
      *out_status = Z7_MI_STATUS_INTERNAL_ERROR;
    }
    return false;
  }

  uint64_t aggregated_directory_file_count = 0;
  QHash<QString, std::optional<z7::app::ListResult>> parent_list_cache;

  out_prepared_items->reserve(items.size());
  for (size_t index = 0; index < items.size(); ++index) {
    const QuickLookBatchExportItem& item = items[index];
    QuickLookBatchExportPreparedItem prepared;
    prepared.item = item;
    prepared.failure_entry_path = item.entry_path;
    prepared.failure_destination_path = item.destination_path;
    prepared.normalized_entry_path =
        normalize_batch_export_entry_path(item.entry_path, nested_archive_entries);

    z7::app::GetEntryInfoRequest request;
    request.session_token = (*out_chain)->tokens.back();
    request.entry_path =
        z7::ui::archive_support::to_utf8_string(prepared.normalized_entry_path);
    const z7::app::OperationOutcome outcome = capi::run_archive_request_sync(
        z7::app::ArchiveRequest{request}, task_state);
    const auto payload =
        z7::app::outcome_payload_as<z7::app::GetEntryInfoResult>(outcome);
    if (!payload.has_value() || !payload->ok) {
      if (out_status != nullptr) {
        *out_status = quicklook_batch_export_status_from_outcome(outcome, task_state);
      }
      *failed_item_index = static_cast<int64_t>(index);
      *failed_entry_path = prepared.failure_entry_path;
      *failed_destination_path = prepared.failure_destination_path;
      *error_message = capi::fallback_archive_error_summary(outcome);
      return false;
    }
    if (!payload->exists) {
      *failed_item_index = static_cast<int64_t>(index);
      *failed_entry_path = prepared.failure_entry_path;
      *failed_destination_path = prepared.failure_destination_path;
      *error_message =
          QStringLiteral("Quick Look batch export entry does not exist in archive.");
      if (out_status != nullptr) {
        *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
      }
      return false;
    }
    if (payload->is_directory != item.entry_is_directory) {
      *failed_item_index = static_cast<int64_t>(index);
      *failed_entry_path = prepared.failure_entry_path;
      *failed_destination_path = prepared.failure_destination_path;
      *error_message =
          QStringLiteral("Quick Look batch export entry type does not match archive.");
      if (out_status != nullptr) {
        *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
      }
      return false;
    }
    if (payload->is_directory && !item.recursive) {
      *failed_item_index = static_cast<int64_t>(index);
      *failed_entry_path = prepared.failure_entry_path;
      *failed_destination_path = prepared.failure_destination_path;
      *error_message =
          QStringLiteral("Quick Look batch export directory requires recursive extraction.");
      if (out_status != nullptr) {
        *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
      }
      return false;
    }

    if (payload->is_directory) {
      if (!payload->subtree_total_size.has_value() ||
          !payload->subtree_file_count.has_value()) {
        *failed_item_index = static_cast<int64_t>(index);
        *failed_entry_path = prepared.failure_entry_path;
        *failed_destination_path = prepared.failure_destination_path;
        *error_message = QStringLiteral(
            "Quick Look batch export directory statistics unavailable.");
        if (out_status != nullptr) {
          *out_status = Z7_MI_STATUS_INTERNAL_ERROR;
        }
        return false;
      }

      prepared.size_known = true;
      prepared.known_size = *payload->subtree_total_size;
      if (prepared.known_size > capi::kQuicklookBatchExportMaxBytes) {
        *failed_item_index = static_cast<int64_t>(index);
        *failed_entry_path = prepared.failure_entry_path;
        *failed_destination_path = prepared.failure_destination_path;
        *error_message = QStringLiteral(
            "Quick Look batch export directory too large: more than 1 GiB.");
        if (out_status != nullptr) {
          *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
        }
        return false;
      }

      prepared.file_count_known = true;
      prepared.known_file_count = *payload->subtree_file_count;
      aggregated_directory_file_count += prepared.known_file_count;
      if (aggregated_directory_file_count >
          capi::kQuicklookBatchExportDirectoryMaxFiles) {
        *failed_item_index = static_cast<int64_t>(index);
        *failed_entry_path = prepared.failure_entry_path;
        *failed_destination_path = prepared.failure_destination_path;
        *error_message = QStringLiteral(
            "Quick Look batch export selects more than 1000 files across directories.");
        if (out_status != nullptr) {
          *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
        }
        return false;
      }
    } else {
      prepared.size_known = true;
      prepared.known_size = payload->size;
      if (prepared.known_size == 0) {
        prepared.known_size = prepared.item.listed_size;
      }
      if (prepared.known_size == 0) {
        const int slash_index = prepared.normalized_entry_path.lastIndexOf(QLatin1Char('/'));
        const QString parent_dir =
            slash_index < 0 ? QString() : prepared.normalized_entry_path.left(slash_index);
        const QString child_name =
            slash_index < 0 ? prepared.normalized_entry_path
                            : prepared.normalized_entry_path.mid(slash_index + 1);
        if (!parent_list_cache.contains(parent_dir)) {
          z7::app::ListRequest list_request;
          list_request.session_token = (*out_chain)->tokens.back();
          list_request.directory = z7::ui::archive_support::to_utf8_string(parent_dir);
          list_request.include_detailed_props = true;
          const z7::app::OperationOutcome list_outcome = capi::run_archive_request_sync(
              z7::app::ArchiveRequest{list_request}, task_state);
          const auto list_payload =
              z7::app::outcome_payload_as<z7::app::ListResult>(list_outcome);
          if (!list_payload.has_value() || !list_payload->ok) {
            if (out_status != nullptr) {
              *out_status =
                  quicklook_batch_export_status_from_outcome(list_outcome, task_state);
            }
            *failed_item_index = static_cast<int64_t>(index);
            *failed_entry_path = prepared.failure_entry_path;
            *failed_destination_path = prepared.failure_destination_path;
            *error_message = capi::fallback_archive_error_summary(list_outcome);
            return false;
          }
          parent_list_cache.insert(parent_dir, *list_payload);
        }

        const std::optional<z7::app::ListResult>& cached_list =
            parent_list_cache.value(parent_dir);
        if (cached_list.has_value()) {
          for (const z7::app::ArchiveListEntry& list_entry : cached_list->entries) {
            const QString list_name =
                z7::ui::archive_support::from_native_string(list_entry.path);
            if (list_name == child_name && !list_entry.is_dir) {
              prepared.known_size = list_entry.size;
              break;
            }
          }
        }
      }
      if (prepared.known_size > capi::kQuicklookBatchExportMaxBytes) {
        *failed_item_index = static_cast<int64_t>(index);
        *failed_entry_path = prepared.failure_entry_path;
        *failed_destination_path = prepared.failure_destination_path;
        *error_message =
            QStringLiteral("Quick Look batch export file too large: more than 1 GiB.");
        if (out_status != nullptr) {
          *out_status = Z7_MI_STATUS_INVALID_ARGUMENT;
        }
        return false;
      }
    }

    out_prepared_items->push_back(std::move(prepared));
  }

  return true;
}

}  // namespace

extern "C" {

z7_mi_status_t z7_mi_quicklook_batch_export(
    z7_mi_session_t* session,
    const z7_mi_quicklook_batch_export_request_t* request,
    z7_mi_quicklook_batch_export_progress_callback_t progress_callback,
    z7_mi_quicklook_batch_export_callback_t callback,
    void* user_data,
    z7_mi_task_t** out_task) {
  if (out_task != nullptr) {
    *out_task = nullptr;
  }
  if (session == nullptr || request == nullptr || callback == nullptr ||
      out_task == nullptr || request->item_count == 0 || request->items == nullptr) {
    (void)user_data;
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }

  capi::ensure_qt_core_app();

  const QString archive_path =
      QFileInfo(capi::to_qstring(request->archive_path)).absoluteFilePath();
  if (archive_path.isEmpty()) {
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  const QString archive_type_hint = capi::to_qstring(request->archive_type_hint);
  QString nested_error;
  const QStringList nested_archive_entries = capi::normalize_nested_archive_entries(
      request->nested_archive_entries,
      request->nested_archive_entry_count,
      &nested_error);
  if (!nested_error.isEmpty() || nested_archive_entries.size() > 5) {
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }

  std::vector<QuickLookBatchExportItem> items;
  items.reserve(request->item_count);
  for (size_t index = 0; index < request->item_count; ++index) {
    const z7_mi_quicklook_batch_export_item_t& item = request->items[index];
    QuickLookBatchExportItem native_item;
    native_item.entry_path = capi::to_qstring(item.entry_path);
    native_item.destination_path = QFileInfo(
        capi::to_qstring(item.destination_path)).absoluteFilePath();
    native_item.listed_size = item.listed_size;
    native_item.recursive = item.recursive;
    native_item.entry_is_directory = item.entry_is_directory;
    if (native_item.destination_path.trimmed().isEmpty()) {
      return Z7_MI_STATUS_INVALID_ARGUMENT;
    }
    if (!native_item.entry_is_directory &&
        native_item.entry_path.trimmed().isEmpty()) {
      return Z7_MI_STATUS_INVALID_ARGUMENT;
    }
    items.push_back(std::move(native_item));
  }

  const auto state = session->state;
  auto task_state = std::make_shared<AsyncTaskState>();
  const uint64_t task_id = capi::register_in_flight_task(state, task_state);
  if (task_id == 0) {
    return Z7_MI_STATUS_INTERNAL_ERROR;
  }
  auto* task_handle = new (std::nothrow) z7_mi_task_t();
  if (task_handle == nullptr) {
    capi::unregister_in_flight_task(state, task_id);
    return Z7_MI_STATUS_INTERNAL_ERROR;
  }
  task_handle->state = state;
  task_handle->task_id = task_id;
  *out_task = task_handle;

  const std::weak_ptr<z7_mi_session_state> weak_state(state);
  std::thread([callback,
               progress_callback,
               user_data,
               state = state,
               archive_path,
               archive_type_hint,
               nested_archive_entries,
               items = std::move(items),
               weak_state,
               task_state,
               task_id]() mutable {
    std::shared_ptr<NestedSessionChain> chain;
    std::vector<QuickLookBatchExportPreparedItem> prepared_items;
    z7_mi_quicklook_batch_export_result_t* out_result = nullptr;

    QString error_message;
    z7_mi_status_t preflight_status = Z7_MI_STATUS_INVALID_ARGUMENT;
    int64_t failed_item_index = -1;
    QString failed_entry_path;
    QString failed_destination_path;
    const bool prepared = prepare_batch_export_items(
        state,
        archive_path,
        archive_type_hint,
        nested_archive_entries,
        task_state,
        items,
        &chain,
        &prepared_items,
        &error_message,
        &preflight_status,
        &failed_item_index,
        &failed_entry_path,
        &failed_destination_path);
    if (!prepared) {
      out_result = capi::allocate_result<z7_mi_quicklook_batch_export_result_t>();
      if (out_result != nullptr) {
        capi::init_quicklook_batch_export_error(
            out_result,
            preflight_status,
            error_message.trimmed().isEmpty()
                ? QStringLiteral("Quick Look batch export preflight failed.")
                : error_message,
            0,
            items.size(),
            failed_item_index,
            failed_entry_path,
            failed_destination_path);
      }
    } else {
      auto delegate = std::make_shared<QuickLookBatchExportDelegate>(
          progress_callback,
          user_data,
          prepared_items.size());
      const QString effective_archive_type =
          capi::infer_archive_format(archive_path, archive_type_hint)
              .trimmed()
              .toLower();
      auto password_delegate = capi::make_quicklook_password_relay_delegate(
          state,
          archive_path,
          effective_archive_type,
          nested_archive_entries,
          task_state,
          delegate);

      size_t completed_item_count = 0;
      for (size_t index = 0; index < prepared_items.size(); ++index) {
        if (task_state->cancel_requested.load()) {
          out_result = capi::allocate_result<z7_mi_quicklook_batch_export_result_t>();
          if (out_result != nullptr) {
            capi::init_quicklook_batch_export_error(
                out_result,
                Z7_MI_STATUS_CANCELED,
                QStringLiteral("Operation canceled."),
                completed_item_count,
                prepared_items.size(),
                static_cast<int64_t>(index),
                prepared_items[index].failure_entry_path,
                prepared_items[index].failure_destination_path);
          }
          break;
        }

        const QuickLookBatchExportPreparedItem& prepared_item = prepared_items[index];
        delegate->set_item_context(
            completed_item_count,
            index,
            prepared_item.failure_entry_path,
            prepared_item.failure_destination_path);
        delegate->emit_indeterminate_progress(
            QStringLiteral("Extracting %1 of %2")
                .arg(index + 1)
                .arg(prepared_items.size()));

        const z7::ui::archive_support::QuickLookExtractPlan extract_plan =
            z7::ui::archive_support::build_quicklook_extract_plan(
                prepared_item.normalized_entry_path,
                prepared_item.item.entry_is_directory,
                prepared_item.item.destination_path);

        z7::app::ExtractRequest extract_request;
        extract_request.session_token = chain->tokens.back();
        extract_request.archive_type_hint =
            z7::ui::archive_support::to_utf8_string(archive_type_hint);
        extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
        extract_request.path_mode = z7::app::ExtractPathMode::kFullPaths;
        extract_request.output_dir =
            z7::ui::archive_support::to_native_string(extract_plan.output_dir);
        extract_request.eliminate_root_duplication = false;
        if (!prepared_item.normalized_entry_path.isEmpty()) {
          extract_request.entries.push_back(
              z7::ui::archive_support::to_utf8_string(
                  prepared_item.normalized_entry_path));
        }
        extract_request.path_remaps.push_back(extract_plan.path_remap);

        const z7::app::OperationOutcome outcome = capi::run_archive_request_sync(
            z7::app::ArchiveRequest{std::move(extract_request)},
            task_state,
            password_delegate);
        const auto payload =
            z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
        if (!payload.has_value() || !payload->ok) {
          const z7_mi_status_t status =
              quicklook_batch_export_status_from_outcome(outcome, task_state);
          out_result = capi::allocate_result<z7_mi_quicklook_batch_export_result_t>();
          if (out_result != nullptr) {
            capi::init_quicklook_batch_export_error(
                out_result,
                status,
                capi::fallback_archive_error_summary(outcome),
                completed_item_count,
                prepared_items.size(),
                static_cast<int64_t>(index),
                prepared_item.failure_entry_path,
                prepared_item.failure_destination_path);
          }
          break;
        }

        ++completed_item_count;
      }

      if (out_result == nullptr) {
        out_result = build_batch_export_success_result(
            prepared_items.size(), prepared_items.size());
      }
    }

    capi::clear_active_archive_session(task_state);
    capi::release_nested_session_chain(state, chain);
    capi::mark_task_completed(task_state);
    const bool should_dispatch = capi::report_once(task_state);
    capi::unregister_in_flight_task(weak_state, task_id);
    if (should_dispatch) {
      capi::dispatch_result(out_result, callback, user_data);
    } else {
      z7_mi_destroy_quicklook_batch_export_result(out_result);
    }
  }).detach();

  return Z7_MI_STATUS_OK;
}

void z7_mi_free_quicklook_batch_export_progress(
    z7_mi_quicklook_batch_export_progress_t* progress) {
  free_batch_export_progress_strings(progress);
}

void z7_mi_destroy_quicklook_batch_export_progress(
    z7_mi_quicklook_batch_export_progress_t* progress) {
  if (progress == nullptr) {
    return;
  }
  free_batch_export_progress_strings(progress);
  std::free(progress);
}

void z7_mi_free_quicklook_batch_export_result(
    z7_mi_quicklook_batch_export_result_t* result) {
  free_batch_export_result_strings(result);
}

void z7_mi_destroy_quicklook_batch_export_result(
    z7_mi_quicklook_batch_export_result_t* result) {
  if (result == nullptr) {
    return;
  }
  free_batch_export_result_strings(result);
  std::free(result);
}

}  // extern "C"
