// src/archive_application/src/native_7z/callbacks/callbacks_extract_get_stream.cpp
// Role: Extract callback output stream selection and materialization setup.

#include "core/internal.h"
#include "core/filesystem_replace.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"

namespace z7::app {
namespace {

bool restore_backup_to_destination(const fs::path& backup_path,
                                   const fs::path& destination_path,
                                   std::error_code& ec) {
  ec.clear();
  if (backup_path.empty()) {
    return true;
  }

  std::error_code cleanup_ec;
  remove_path_any(destination_path, cleanup_ec);
  ec.clear();
  fs::rename(backup_path, destination_path, ec);
  return !ec;
}

fs::path make_partial_output_path(const fs::path& destination_path,
                                  std::error_code& ec) {
  return make_unique_sibling_path(destination_path, ".partial-", ec);
}

}  // namespace

STDMETHODIMP NativeExtractCallback::GetStream(UInt32 index,
                                              ISequentialOutStream** out_stream,
                                              Int32 ask_extract_mode) throw() {
  if (out_stream == nullptr) {
    return E_INVALIDARG;
  }
  *out_stream = nullptr;

  std::string archive_entry_path = normalize_archive_item_path(
      archive_get_prop_text(archive_, index, kpidPath));
  if (!archive_entry_path.empty() &&
      !archive_virtual_path_is_safe_for_materialization(archive_entry_path)) {
    record_io_error("Unsafe archive entry path escapes destination: " +
                    archive_entry_path);
    return E_FAIL;
  }

  bool is_dir = false;
  (void)archive_get_prop_bool(archive_, index, kpidIsDir, is_dir);
  bool is_encrypted = false;
  (void)archive_get_prop_bool(archive_, index, kpidEncrypted, is_encrypted);
  const std::string display_path =
      archive_entry_path.empty() ? std::to_string(index) : archive_entry_path;
  const std::string output_item_path =
      archive_entry_path.empty() && !is_dir ? display_path : archive_entry_path;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_path_ = display_path;
    current_item_encrypted_ = is_encrypted;
  }
  emit_progress_snapshot();

  const HRESULT canceled_check = check_canceled();
  if (canceled_check != S_OK) {
    return canceled_check;
  }

  if (restore_file_security_ && !security_notice_emitted_) {
    security_notice_emitted_ = true;
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kNone,
                   "Restore file security is enabled.");
  }

  if (ask_extract_mode != NArchive::NExtract::NAskMode::kExtract) {
    return S_OK;
  }

  // Budget check: count files (dirs + regular files each count as one entry).
  if (budget_.has_value() && budget_->max_files.has_value()) {
    const uint64_t seen =
        budget_files_seen_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (seen > *budget_->max_files) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!budget_triggered_.exchange(true, std::memory_order_acq_rel)) {
        budget_trigger_reason_ =
            "max_files limit exceeded (" + std::to_string(*budget_->max_files) + ")";
      }
      return E_ABORT;
    }
  }

  if (buffer_sink_ != nullptr) {
    if (is_dir) {
      return S_OK;
    }
    auto* stream = new NativeBufferOutStream(*buffer_sink_, buffer_sink_max_size_);
    *out_stream = stream;
    return S_OK;
  }

  const ResolvedPath resolved_path = resolve_destination_path(output_item_path);
  const fs::path destination_path = resolved_path.destination_path;

  std::error_code ec;
  if (is_dir) {
    const bool existed_before = fs::exists(destination_path, ec);
    if (ec) {
      record_io_error("Cannot query output path: " +
                      destination_path.generic_string());
      return E_FAIL;
    }
    fs::create_directories(destination_path, ec);
    if (ec) {
      record_io_error("Cannot create output directory: " +
                      destination_path.generic_string());
      return E_FAIL;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ExtractMaterializedEntry dir_entry;
      dir_entry.archive_entry_path = output_item_path;
      dir_entry.absolute_output_path = resolved_path.absolute_output_path;
      dir_entry.is_directory = true;
      materialized_entries_.push_back(std::move(dir_entry));
      if (!existed_before) {
        ExtractRollbackEntry rollback_entry;
        rollback_entry.output_path = destination_path;
        rollback_entry.destination_path = destination_path;
        rollback_entry.is_directory = true;
        rollback_entries_.push_back(std::move(rollback_entry));
      }
    }
    return S_OK;
  }

  fs::path final_output_path = destination_path;
  fs::path staged_output_path;
  fs::path backup_path;
  bool had_original = false;
  bool restore_backup_on_failure = false;
  bool preserve_backup_on_commit = false;
  bool preserve_committed_backup_for_rollback =
      budget_.has_value() &&
      budget_->on_exceeded == BudgetExceededAction::kFailAndRollback;
  ec.clear();
  const bool exists = fs::exists(destination_path, ec);
  if (ec) {
    record_io_error("Cannot query output path: " +
                    destination_path.generic_string());
    return E_FAIL;
  }

  if (exists) {
    switch (overwrite_mode_) {
      case OverwriteMode::kSkip: {
        emit_log_event(hooks_,
                       OperationStage::kRunning,
                       OutputChannel::kNone,
                       "Skipping existing file: " + destination_path.generic_string());
        return S_OK;
      }
      case OverwriteMode::kAsk: {
        if (ask_yes_to_all_) {
          had_original = true;
          break;
        }
        if (ask_no_to_all_) {
          emit_log_event(hooks_,
                         OperationStage::kRunning,
                         OutputChannel::kNone,
                         "Skipping existing file: " + destination_path.generic_string());
          return S_OK;
        }

        const OverwriteDecision decision =
            ask_overwrite_decision(destination_path, index, output_item_path);
        switch (decision) {
          case OverwriteDecision::kYes: {
            had_original = true;
            break;
          }
          case OverwriteDecision::kYesToAll: {
            ask_yes_to_all_ = true;
            had_original = true;
            break;
          }
          case OverwriteDecision::kNo: {
            emit_log_event(hooks_,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Skipping existing file: " + destination_path.generic_string());
            return S_OK;
          }
          case OverwriteDecision::kNoToAll: {
            ask_no_to_all_ = true;
            emit_log_event(hooks_,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Skipping existing file: " + destination_path.generic_string());
            return S_OK;
          }
          case OverwriteDecision::kAutoRename: {
            std::error_code unique_ec;
            final_output_path =
                make_unique_destination_path(destination_path, unique_ec);
            if (unique_ec || final_output_path.empty()) {
              record_io_error("Cannot allocate renamed output path: " +
                              destination_path.generic_string() +
                              (unique_ec ? std::string("; ") + unique_ec.message() : ""));
              return E_FAIL;
            }
            break;
          }
          case OverwriteDecision::kCancel: {
            return E_ABORT;
          }
        }
        break;
      }
      case OverwriteMode::kOverwrite: {
        had_original = true;
        break;
      }
      case OverwriteMode::kRenameExisting: {
        std::error_code unique_ec;
        const fs::path renamed_existing =
            make_unique_destination_path(destination_path, unique_ec);
        if (unique_ec || renamed_existing.empty()) {
          record_io_error("Cannot allocate renamed destination path: " +
                          destination_path.generic_string() +
                          (unique_ec ? std::string("; ") + unique_ec.message() : ""));
          return E_FAIL;
        }
        fs::rename(destination_path, renamed_existing, ec);
        if (ec) {
          record_io_error("Cannot rename existing destination: " +
                          destination_path.generic_string());
          return E_FAIL;
        }
        backup_path = renamed_existing;
        had_original = true;
        restore_backup_on_failure = true;
        preserve_backup_on_commit = true;
        emit_log_event(hooks_,
                       OperationStage::kRunning,
                       OutputChannel::kNone,
                       "Renamed existing file to: " +
                           renamed_existing.generic_string());
        break;
      }
      case OverwriteMode::kRenameExtracted: {
        std::error_code unique_ec;
        final_output_path =
            make_unique_destination_path(destination_path, unique_ec);
        if (unique_ec || final_output_path.empty()) {
          record_io_error("Cannot allocate renamed output path: " +
                          destination_path.generic_string() +
                          (unique_ec ? std::string("; ") + unique_ec.message() : ""));
          return E_FAIL;
        }
        break;
      }
    }
  }

  std::error_code partial_path_ec;
  staged_output_path = make_partial_output_path(final_output_path, partial_path_ec);
  if (staged_output_path.empty()) {
    if (restore_backup_on_failure &&
        !restore_backup_to_destination(backup_path, destination_path, ec)) {
      record_io_error("Cannot allocate partial output path for extract: " +
                      final_output_path.generic_string() +
                      (partial_path_ec ? std::string("; ") + partial_path_ec.message() : "") +
                      "; original file restore also failed: " + ec.message());
    } else {
      record_io_error("Cannot allocate partial output path for extract: " +
                      final_output_path.generic_string() +
                      (partial_path_ec ? std::string("; ") + partial_path_ec.message() : ""));
    }
    return E_FAIL;
  }

  if (!ensure_parent_dir(staged_output_path, ec)) {
    if (restore_backup_on_failure &&
        !restore_backup_to_destination(backup_path, destination_path, ec)) {
      record_io_error("Cannot create output directory: " +
                      staged_output_path.parent_path().generic_string() +
                      "; original file restore also failed: " + ec.message());
    } else {
      record_io_error("Cannot create output directory: " +
                      staged_output_path.parent_path().generic_string());
    }
    return E_FAIL;
  }

  auto* stream = new NativeFileOutStream(staged_output_path);
  const HRESULT open_res = stream->open();
  if (open_res != S_OK) {
    std::error_code cleanup_ec;
    remove_path_any(staged_output_path, cleanup_ec);
    if (restore_backup_on_failure &&
        !restore_backup_to_destination(backup_path, destination_path, ec)) {
      record_io_error("Cannot create output file: " +
                      staged_output_path.generic_string() +
                      "; original file restore also failed: " + ec.message());
    } else {
      record_io_error("Cannot create output file: " +
                      staged_output_path.generic_string());
    }
    stream->Release();
    return open_res;
  }

  stream->AddRef();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    PendingEntry pe;
    pe.archive_entry_path = output_item_path;
    pe.absolute_output_path =
        (final_output_path == destination_path)
            ? resolved_path.absolute_output_path
            : fs::absolute(final_output_path).generic_string();
    pe.output_path = final_output_path;
    pe.staged_output_path = staged_output_path;
    pe.destination_path = final_output_path;
    pe.backup_path = backup_path;
    pe.had_original = had_original;
    pe.overwrote_existing = exists && (final_output_path == destination_path);
    pe.renamed_from_collision = (final_output_path != destination_path);
    pe.restore_backup_on_failure = restore_backup_on_failure;
    pe.preserve_backup_on_commit = preserve_backup_on_commit;
    pe.preserve_committed_backup_for_rollback =
        preserve_committed_backup_for_rollback && pe.overwrote_existing &&
        !pe.preserve_backup_on_commit;
    (void)archive_get_prop_uint64(archive_, index, kpidSize, pe.declared_size);
    pe.owned_stream = stream;
    pending_entry_ = std::move(pe);
  }

  *out_stream = stream;
  return S_OK;
}

}  // namespace z7::app
