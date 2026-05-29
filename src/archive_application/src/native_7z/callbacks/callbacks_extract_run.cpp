// src/archive_application/src/native_7z/callbacks/callbacks_extract_run.cpp
// Role: Extract callback construction and externally queried state.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

NativeExtractCallback::NativeExtractCallback(
    IInArchive* archive,
    fs::path output_dir,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused,
    std::string archive_path,
    std::vector<std::string> selected_entries,
    OverwriteMode overwrite_mode,
    ExtractPathMode path_mode,
    std::string eliminate_prefix,
    std::vector<ExtractPathRemap> path_remaps,
    std::string password,
    ExtractZoneIdMode zone_id_mode,
    bool restore_file_security,
    uint64_t total_files,
    std::optional<ExtractBudget> budget,
    uint64_t configured_memory_limit_bytes,
    bool configured_memory_limit_defined)
    : CallbackBase(cancel_requested, std::move(wait_while_paused)),
      archive_(archive),
      output_dir_(std::move(output_dir)),
      hooks_(hooks),
      archive_path_(std::move(archive_path)),
      selected_entries_(std::move(selected_entries)),
      overwrite_mode_(overwrite_mode),
      path_mode_(path_mode),
      eliminate_prefix_(normalize_archive_item_path(eliminate_prefix)),
      path_remaps_(std::move(path_remaps)),
      password_(std::move(password)),
      zone_id_mode_(zone_id_mode),
      restore_file_security_(restore_file_security),
      configured_memory_limit_bytes_(configured_memory_limit_bytes),
      configured_memory_limit_defined_(configured_memory_limit_defined &&
                                       configured_memory_limit_bytes != 0),
      total_files_(total_files),
      budget_(std::move(budget)) {
  for (std::string& entry : selected_entries_) {
    entry = normalize_archive_item_path(entry);
  }
  for (ExtractPathRemap& remap : path_remaps_) {
    remap.source_path = normalize_archive_item_path(remap.source_path);
  }
}

void NativeExtractCallback::set_buffer_sink(std::vector<uint8_t>* buffer_sink,
                                             size_t max_size) {
  buffer_sink_ = buffer_sink;
  buffer_sink_max_size_ = max_size;
}

uint64_t NativeExtractCallback::completed_files() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_files_;
}

uint64_t NativeExtractCallback::error_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_count_;
}

bool NativeExtractCallback::totals_known() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return totals_known_;
}

uint64_t NativeExtractCallback::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_bytes_;
}

uint64_t NativeExtractCallback::completed_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_bytes_;
}

std::string NativeExtractCallback::current_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_path_;
}

std::optional<ProgressRatioInfo> NativeExtractCallback::ratio_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ratio_input_size_known_ && !ratio_output_size_known_) {
    return std::nullopt;
  }
  ProgressRatioInfo ratio;
  ratio.input_size_known = ratio_input_size_known_;
  ratio.input_size = ratio_input_size_;
  ratio.output_size_known = ratio_output_size_known_;
  ratio.output_size = ratio_output_size_;
  ratio.compressing_mode = false;
  return ratio;
}

bool NativeExtractCallback::password_requested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return password_requested_;
}

bool NativeExtractCallback::wrong_password() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wrong_password_;
}

bool NativeExtractCallback::has_io_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return io_error_;
}

std::string NativeExtractCallback::io_error_message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return io_error_message_;
}

std::string NativeExtractCallback::diagnostic_message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return diagnostic_message_;
}

std::vector<ExtractMaterializedEntry>
NativeExtractCallback::take_materialized_entries() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::move(materialized_entries_);
}

std::vector<ExtractRollbackEntry> NativeExtractCallback::take_rollback_entries() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::move(rollback_entries_);
}

bool NativeExtractCallback::budget_triggered() const {
  return budget_triggered_.load(std::memory_order_acquire);
}

std::string NativeExtractCallback::budget_trigger_reason() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return budget_trigger_reason_;
}

BudgetExceededAction NativeExtractCallback::budget_policy() const {
  if (budget_.has_value()) {
    return budget_->on_exceeded;
  }
  return BudgetExceededAction::kFailAndRollback;
}

bool NativeExtractCallback::request_selects_single_logical_root() const {
  if (selected_entries_.empty()) {
    return true;
  }
  return selected_entries_.size() == 1;
}

}  // namespace z7::app
