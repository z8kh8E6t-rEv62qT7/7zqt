#include "internal.h"
#include "password_relay_delegate.h"

#include <QFileInfo>

#include <cstring>
#include <thread>
#include <vector>

namespace capi = z7::macos_integration::capi_internal;

namespace {

class StreamingListCollectorDelegate final : public z7::app::IArchiveDelegate {
 public:
  explicit StreamingListCollectorDelegate(std::shared_ptr<AsyncTaskState> task_state)
      : task_state_(std::move(task_state)) {}

  bool on_list_entries_batch(std::vector<z7::app::ArchiveListEntry>&& batch) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& entry : batch) {
        entries_.push_back(std::move(entry));
      }
    }
    return !(task_state_ && task_state_->cancel_requested.load());
  }

  std::vector<z7::app::ArchiveListEntry> take_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(entries_);
  }

 private:
  std::shared_ptr<AsyncTaskState> task_state_;
  std::mutex mutex_;
  std::vector<z7::app::ArchiveListEntry> entries_;
};

z7_mi_quicklook_list_result_t* build_quicklook_list_success_result(
    const QString& archive_path,
    const QString& virtual_dir,
    const std::vector<z7::app::ArchiveListEntry>& entries) {
  z7_mi_quicklook_list_result_t* out_result =
      capi::allocate_result<z7_mi_quicklook_list_result_t>();
  if (out_result == nullptr) {
    return nullptr;
  }
  out_result->ok = true;
  out_result->status = Z7_MI_STATUS_OK;
  out_result->archive_path = capi::duplicate_c_string(archive_path);
  out_result->virtual_dir = capi::duplicate_c_string(virtual_dir);
  out_result->item_count = entries.size();
  if (out_result->item_count == 0) {
    return out_result;
  }

  out_result->items = static_cast<z7_mi_quicklook_item_t*>(
      std::calloc(out_result->item_count, sizeof(z7_mi_quicklook_item_t)));
  if (out_result->items == nullptr) {
    z7_mi_free_quicklook_list_result(out_result);
    capi::init_quicklook_list_error(out_result,
                                    Z7_MI_STATUS_INTERNAL_ERROR,
                                    QStringLiteral("Out of memory allocating quicklook items."));
    return out_result;
  }

  for (size_t i = 0; i < out_result->item_count; ++i) {
    const z7::app::ArchiveListEntry& entry = entries[i];
    const QString name = z7::ui::archive_support::from_native_string(entry.path);
    out_result->items[i].name = capi::duplicate_c_string(name);
    out_result->items[i].path = capi::duplicate_c_string(
        capi::join_virtual_dir_with_name(virtual_dir, name));
    out_result->items[i].is_dir = entry.is_dir;
    out_result->items[i].size = entry.size;
    out_result->items[i].mtime_msecs_utc =
        entry.mtime_msecs_utc.has_value() ? *entry.mtime_msecs_utc : -1;
    out_result->items[i].is_archive_like =
        !entry.is_dir && capi::is_archive_like_by_name(name);
  }
  return out_result;
}

}  // namespace

extern "C" {

z7_mi_status_t z7_mi_quicklook_list(z7_mi_session_t* session,
                                    const z7_mi_quicklook_list_request_t* request,
                                    z7_mi_quicklook_list_callback_t callback,
                                    void* user_data,
                                    z7_mi_task_t** out_task) {
  if (out_task != nullptr) {
    *out_task = nullptr;
  }
  if (session == nullptr || request == nullptr || callback == nullptr ||
      out_task == nullptr) {
    (void)user_data;
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  capi::ensure_qt_core_app();

  const QString archive_path =
      QFileInfo(capi::to_qstring(request->archive_path)).absoluteFilePath();
  if (archive_path.isEmpty()) {
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  const QString virtual_dir =
      capi::normalize_virtual_entry_path(capi::to_qstring(request->virtual_dir));
  const QString archive_type_hint = capi::to_qstring(request->archive_type_hint);
  const QString effective_archive_type =
      capi::infer_archive_format(archive_path, archive_type_hint)
          .trimmed()
          .toLower();
  QString nested_error;
  const QStringList nested_archive_entries = capi::normalize_nested_archive_entries(
      request->nested_archive_entries,
      request->nested_archive_entry_count,
      &nested_error);
  if (!nested_error.isEmpty()) {
    return Z7_MI_STATUS_INVALID_ARGUMENT;
  }
  if (nested_archive_entries.size() > 5) {
    return Z7_MI_STATUS_INVALID_ARGUMENT;
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
               user_data,
               state = state,
               archive_path,
               virtual_dir,
               archive_type_hint,
               effective_archive_type,
               nested_archive_entries,
               weak_state,
               task_state,
               task_id]() mutable {
    std::shared_ptr<NestedSessionChain> chain;
    z7_mi_quicklook_list_result_t* out_result = nullptr;

    QString chain_error;
    const z7_mi_status_t chain_status = capi::open_nested_session_chain(
        state,
        archive_path,
        archive_type_hint,
        nested_archive_entries,
        task_state,
        &chain,
        &chain_error);
    if (chain_status != Z7_MI_STATUS_OK) {
      out_result = capi::allocate_result<z7_mi_quicklook_list_result_t>();
      if (out_result != nullptr) {
        capi::init_quicklook_list_error(out_result, chain_status, chain_error);
      }
    } else if (!chain || chain->tokens.empty()) {
      out_result = capi::allocate_result<z7_mi_quicklook_list_result_t>();
      if (out_result != nullptr) {
        capi::init_quicklook_list_error(out_result,
                                        Z7_MI_STATUS_INTERNAL_ERROR,
                                        QStringLiteral("Nested session chain was empty."));
      }
    } else {
      auto collector = std::make_shared<StreamingListCollectorDelegate>(task_state);
      z7::app::ListRequest list_request;
      list_request.session_token = chain->tokens.back();
      list_request.directory = z7::ui::archive_support::to_utf8_string(virtual_dir);
      list_request.include_detailed_props = true;
      list_request.streaming_mode = true;
      list_request.batch_size_hint = 256;
      auto password_delegate = capi::make_quicklook_password_relay_delegate(
          state,
          archive_path,
          effective_archive_type,
          nested_archive_entries,
          task_state,
          collector);

      const z7::app::OperationOutcome outcome = capi::run_archive_request_sync(
          z7::app::ArchiveRequest{list_request}, task_state, password_delegate);
      if (capi::should_report_canceled(outcome, task_state->cancel_requested.load())) {
        out_result = capi::allocate_result<z7_mi_quicklook_list_result_t>();
        if (out_result != nullptr) {
          capi::init_quicklook_list_error(out_result,
                                          Z7_MI_STATUS_CANCELED,
                                          capi::canceled_message(outcome));
        }
      } else if (outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword) {
        out_result = capi::allocate_result<z7_mi_quicklook_list_result_t>();
        if (out_result != nullptr) {
          capi::init_quicklook_list_error(
              out_result,
              task_state->password_prompt_canceled.load()
                  ? Z7_MI_STATUS_PASSWORD_REQUIRED
                  : Z7_MI_STATUS_BACKEND_ERROR,
              capi::fallback_archive_error_summary(outcome));
        }
      } else {
        const auto payload = z7::app::outcome_payload_as<z7::app::ListResult>(outcome);
        if (!payload.has_value() || !payload->ok) {
          out_result = capi::allocate_result<z7_mi_quicklook_list_result_t>();
          if (out_result != nullptr) {
            capi::init_quicklook_list_error(
                out_result,
                Z7_MI_STATUS_BACKEND_ERROR,
                capi::fallback_archive_error_summary(outcome));
          }
        } else {
          std::vector<z7::app::ArchiveListEntry> entries = collector->take_entries();
          if (entries.empty() && !payload->entries.empty()) {
            entries = payload->entries;
          }
          out_result = build_quicklook_list_success_result(
              archive_path, virtual_dir, entries);
        }
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
      z7_mi_destroy_quicklook_list_result(out_result);
    }
  }).detach();

  return Z7_MI_STATUS_OK;
}

void z7_mi_free_quicklook_list_result(z7_mi_quicklook_list_result_t* result) {
  if (result == nullptr) {
    return;
  }
  capi::free_c_string(result->error_message);
  capi::free_c_string(result->archive_path);
  capi::free_c_string(result->virtual_dir);
  if (result->items != nullptr) {
    for (size_t i = 0; i < result->item_count; ++i) {
      capi::free_c_string(result->items[i].path);
      capi::free_c_string(result->items[i].name);
    }
    std::free(result->items);
    result->items = nullptr;
  }
  result->item_count = 0;
}

void z7_mi_destroy_quicklook_list_result(z7_mi_quicklook_list_result_t* result) {
  if (result == nullptr) {
    return;
  }
  z7_mi_free_quicklook_list_result(result);
  std::free(result);
}

}  // extern "C"
