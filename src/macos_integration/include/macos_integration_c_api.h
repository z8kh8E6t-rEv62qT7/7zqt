#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct z7_mi_session z7_mi_session_t;
typedef struct z7_mi_task z7_mi_task_t;
typedef struct z7_mi_password_prompt z7_mi_password_prompt_t;

typedef enum z7_mi_status {
  Z7_MI_STATUS_OK = 0,
  Z7_MI_STATUS_INVALID_ARGUMENT = 1,
  Z7_MI_STATUS_BACKEND_ERROR = 2,
  Z7_MI_STATUS_IO_ERROR = 3,
  Z7_MI_STATUS_INTERNAL_ERROR = 4,
  Z7_MI_STATUS_CANCELED = 5,
  Z7_MI_STATUS_PASSWORD_REQUIRED = 6
} z7_mi_status_t;

typedef struct z7_mi_selection {
  const char* const* selected_paths;
  size_t selected_path_count;
  bool shift_pressed;
  const char* working_directory;
  const char* locale_hint;
} z7_mi_selection_t;

typedef struct z7_mi_menu_action {
  const char* action_id;
  const char* title;
} z7_mi_menu_action_t;

typedef struct z7_mi_menu_plan {
  bool ok;
  z7_mi_status_t status;
  const char* error_message;
  bool menu_visible;
  const char* base_folder;
  const char* extract_subdir;
  const char* archive_name;
  size_t action_count;
  z7_mi_menu_action_t* actions;
} z7_mi_menu_plan_t;

typedef struct z7_mi_action_result {
  bool ok;
  z7_mi_status_t status;
  const char* error_message;
  const char* action_id;
} z7_mi_action_result_t;

typedef struct z7_mi_quicklook_list_request {
  const char* archive_path;
  const char* virtual_dir;
  const char* archive_type_hint;
  const char* const* nested_archive_entries;
  size_t nested_archive_entry_count;
} z7_mi_quicklook_list_request_t;

typedef struct z7_mi_quicklook_item {
  const char* path;
  const char* name;
  bool is_dir;
  uint64_t size;
  int64_t mtime_msecs_utc;
  bool is_archive_like;
} z7_mi_quicklook_item_t;

typedef struct z7_mi_quicklook_list_result {
  bool ok;
  z7_mi_status_t status;
  const char* error_message;
  const char* archive_path;
  const char* virtual_dir;
  size_t item_count;
  z7_mi_quicklook_item_t* items;
} z7_mi_quicklook_list_result_t;

typedef struct z7_mi_quicklook_batch_export_item {
  const char* entry_path;
  const char* destination_path;
  uint64_t listed_size;
  bool recursive;
  bool entry_is_directory;
} z7_mi_quicklook_batch_export_item_t;

typedef struct z7_mi_quicklook_batch_export_request {
  const char* archive_path;
  const char* archive_type_hint;
  const char* const* nested_archive_entries;
  size_t nested_archive_entry_count;
  const z7_mi_quicklook_batch_export_item_t* items;
  size_t item_count;
} z7_mi_quicklook_batch_export_request_t;

typedef struct z7_mi_quicklook_batch_export_progress {
  size_t completed_item_count;
  size_t total_item_count;
  size_t current_item_index;
  const char* current_entry_path;
  const char* current_destination_path;
  int current_percent;
  bool totals_known;
  uint64_t total_bytes;
  uint64_t completed_bytes;
  const char* current_path;
  const char* message;
} z7_mi_quicklook_batch_export_progress_t;

typedef struct z7_mi_quicklook_batch_export_result {
  bool ok;
  z7_mi_status_t status;
  const char* error_message;
  size_t completed_item_count;
  size_t total_item_count;
  int64_t failed_item_index;
  const char* failed_entry_path;
  const char* failed_destination_path;
} z7_mi_quicklook_batch_export_result_t;

typedef void (*z7_mi_quicklook_list_callback_t)(
    // Result is heap-allocated by C API. Caller must destroy with
    // z7_mi_destroy_quicklook_list_result().
    // Callback always runs on an internal background worker thread.
    z7_mi_quicklook_list_result_t* result,
    void* user_data);

typedef void (*z7_mi_quicklook_batch_export_progress_callback_t)(
    z7_mi_quicklook_batch_export_progress_t* progress,
    void* user_data);

typedef void (*z7_mi_quicklook_batch_export_callback_t)(
    z7_mi_quicklook_batch_export_result_t* result,
    void* user_data);

typedef void (*z7_mi_password_prompt_callback_t)(
    z7_mi_password_prompt_t* handle,
    const char* archive_path,
    const char* const* nested_chain,
    size_t nested_chain_count,
    const char* reason_key,
    void* user_data);

z7_mi_session_t* z7_mi_session_create(void);
void z7_mi_session_destroy(z7_mi_session_t* session);
void z7_mi_session_set_password_prompt_callback(
    z7_mi_session_t* session,
    z7_mi_password_prompt_callback_t callback,
    void* user_data);

z7_mi_status_t z7_mi_build_menu_plan(z7_mi_session_t* session,
                                     const z7_mi_selection_t* selection,
                                     z7_mi_menu_plan_t* out_plan);
void z7_mi_free_menu_plan(z7_mi_menu_plan_t* plan);

z7_mi_status_t z7_mi_execute_menu_action(z7_mi_session_t* session,
                                         const char* action_id,
                                         const z7_mi_selection_t* selection,
                                         z7_mi_action_result_t* out_result);
void z7_mi_free_action_result(z7_mi_action_result_t* result);

// Returns immediate launch status.
// Callback is invoked exactly once only when return value is Z7_MI_STATUS_OK.
z7_mi_status_t z7_mi_quicklook_list(z7_mi_session_t* session,
                                    const z7_mi_quicklook_list_request_t* request,
                                    z7_mi_quicklook_list_callback_t callback,
                                    void* user_data,
                                    // On Z7_MI_STATUS_OK, receives a task handle owned by caller.
                                    // Caller must eventually call z7_mi_task_release().
                                    z7_mi_task_t** out_task);
void z7_mi_free_quicklook_list_result(z7_mi_quicklook_list_result_t* result);
void z7_mi_destroy_quicklook_list_result(z7_mi_quicklook_list_result_t* result);

z7_mi_status_t z7_mi_quicklook_batch_export(
    z7_mi_session_t* session,
    const z7_mi_quicklook_batch_export_request_t* request,
    z7_mi_quicklook_batch_export_progress_callback_t progress_callback,
    z7_mi_quicklook_batch_export_callback_t callback,
    void* user_data,
    z7_mi_task_t** out_task);
void z7_mi_free_quicklook_batch_export_progress(
    z7_mi_quicklook_batch_export_progress_t* progress);
void z7_mi_destroy_quicklook_batch_export_progress(
    z7_mi_quicklook_batch_export_progress_t* progress);
void z7_mi_free_quicklook_batch_export_result(
    z7_mi_quicklook_batch_export_result_t* result);
void z7_mi_destroy_quicklook_batch_export_result(
    z7_mi_quicklook_batch_export_result_t* result);

// Cancel is idempotent and best-effort. Completion callback is still delivered once.
void z7_mi_task_cancel(z7_mi_task_t* task);
void z7_mi_task_release(z7_mi_task_t* task);
void z7_mi_password_prompt_provide(z7_mi_password_prompt_t* handle,
                                   const char* password);
void z7_mi_password_prompt_cancel(z7_mi_password_prompt_t* handle);

#ifdef __cplusplus
}  // extern "C"
#endif
