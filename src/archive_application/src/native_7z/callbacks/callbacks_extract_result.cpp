// src/archive_application/src/native_7z/callbacks/callbacks_extract_result.cpp
// Role: Extract callback operation result accounting.

#include "core/internal.h"
#include "core/filesystem_replace.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"

#include <fstream>
#include <iterator>

namespace z7::app {

namespace {

#if defined(_WIN32) && !defined(UNDER_CE)

fs::path zone_identifier_stream_path(const fs::path& base_path) {
  fs::path stream_path = base_path;
  stream_path += ":Zone.Identifier";
  return stream_path;
}

std::string read_zone_identifier_stream(const fs::path& base_path) {
  std::ifstream in(zone_identifier_stream_path(base_path), std::ios::binary);
  if (!in) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

bool is_office_document_path(const fs::path& output_path) {
  std::string ext = output_path.extension().string();
  if (!ext.empty() && ext.front() == '.') {
    ext.erase(ext.begin());
  }
  for (char& ch : ext) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }

  static constexpr const char* kOfficeExtensions[] = {
      "doc",  "dot",  "wbk",  "docx", "docm", "dotx", "dotm",
      "docb", "wll",  "wwl",  "xls",  "xlt",  "xlm",  "xlsx",
      "xlsm", "xltx", "xltm", "xlsb", "xla",  "xlam", "ppt",
      "pot",  "pps",  "ppa",  "ppam", "pptx", "pptm", "potx",
      "potm", "ppsx", "ppsm", "sldx", "sldm"};
  for (const char* candidate : kOfficeExtensions) {
    if (ext == candidate) {
      return true;
    }
  }
  return false;
}

void write_zone_identifier_stream(const fs::path& output_path,
                                  const std::string& zone_data) {
  if (zone_data.empty()) {
    return;
  }
  std::ofstream out(zone_identifier_stream_path(output_path),
                    std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out.write(zone_data.data(), static_cast<std::streamsize>(zone_data.size()));
}

#endif

}  // namespace

bool NativeExtractCallback::close_pending_entry_stream_locked(
    PendingEntry& pending_entry,
    std::string* close_error_message) {
  if (pending_entry.owned_stream == nullptr) {
    return true;
  }

  NativeFileOutStream* const stream = pending_entry.owned_stream;
  pending_entry.owned_stream = nullptr;
  const HRESULT close_res = stream->Close();
  stream->Release();
  if (close_res == S_OK) {
    return true;
  }

  if (close_error_message != nullptr) {
    *close_error_message =
        "Failed to finalize extracted output: " +
        pending_entry.staged_output_path.generic_string();
  }
  return false;
}

bool NativeExtractCallback::commit_pending_entry_locked(
    PendingEntry& pending_entry,
    std::string* commit_error_message) {
  AtomicReplaceOptions replace_options;
  replace_options.preserve_backup_on_success =
      pending_entry.preserve_committed_backup_for_rollback;
  const AtomicReplaceResult replace_result = replace_file_atomically(
      pending_entry.staged_output_path,
      pending_entry.output_path,
      ".z7-extract-rollback-",
      nullptr,
      &replace_options);
  if (replace_result.success) {
    if (replace_result.destination_existed) {
      if (replace_result.backup_retained) {
        pending_entry.backup_path = replace_result.backup_path;
      } else if (!pending_entry.preserve_backup_on_commit) {
        pending_entry.backup_path.clear();
      }
    }
    pending_entry.destination_path = pending_entry.output_path;
    return true;
  }

  if (commit_error_message != nullptr) {
    if (replace_result.error.has_value() &&
        !replace_result.error->error.message.empty()) {
      *commit_error_message = replace_result.error->error.message;
    } else {
      *commit_error_message =
          "Failed to commit extracted output: " +
          pending_entry.output_path.generic_string();
    }
  }
  return false;
}

bool NativeExtractCallback::restore_pending_entry_original_locked(
    PendingEntry& pending_entry,
    std::string* restore_error_message) {
  std::error_code cleanup_ec;
  remove_path_any(pending_entry.staged_output_path, cleanup_ec);

  if (!pending_entry.restore_backup_on_failure ||
      pending_entry.backup_path.empty()) {
    return true;
  }

  std::error_code restore_ec;
  fs::rename(pending_entry.backup_path, pending_entry.destination_path, restore_ec);
  if (!restore_ec) {
    return true;
  }

  if (restore_error_message != nullptr) {
    *restore_error_message =
        "Failed to restore overwritten output: " + restore_ec.message();
  }
  return false;
}

void NativeExtractCallback::apply_zone_identifier_to_file(
    const fs::path& output_path) const {
#if defined(_WIN32) && !defined(UNDER_CE)
  if (zone_id_mode_ == ExtractZoneIdMode::kNone || archive_path_.empty()) {
    return;
  }
  if (zone_id_mode_ == ExtractZoneIdMode::kOffice &&
      !is_office_document_path(output_path)) {
    return;
  }

  const std::string zone_data =
      read_zone_identifier_stream(fs::path(archive_path_));
  write_zone_identifier_stream(output_path, zone_data);
#else
  (void)output_path;
#endif
}

STDMETHODIMP NativeExtractCallback::SetOperationResult(Int32 op_res) throw() {
  std::string path;
  std::string diagnostic;
  bool force_hresult_failure = false;
  bool encrypted_item = false;
  std::optional<fs::path> zone_identifier_target;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto append_diagnostic_locked = [this](const std::string& message) {
      if (message.empty()) {
        return;
      }
      if (!diagnostic_message_.empty()) {
        diagnostic_message_ += '\n';
      }
      diagnostic_message_ += message;
    };
    path = current_path_;
    encrypted_item = current_item_encrypted_;
    ++completed_files_;
    if (op_res == NArchive::NExtract::NOperationResult::kOK) {
      std::string failure_message;
      if (pending_entry_.has_value()) {
        std::string close_error_message;
        if (!close_pending_entry_stream_locked(*pending_entry_,
                                               &close_error_message)) {
          failure_message = std::move(close_error_message);
        } else {
          std::string commit_error_message;
          if (!commit_pending_entry_locked(*pending_entry_,
                                           &commit_error_message)) {
            failure_message = std::move(commit_error_message);
          } else {
            zone_identifier_target = pending_entry_->output_path;
          }
        }
      }

      if (!failure_message.empty()) {
        std::string restore_error_message;
        if (pending_entry_.has_value() &&
            !restore_pending_entry_original_locked(*pending_entry_,
                                                   &restore_error_message)) {
          append_diagnostic_locked(restore_error_message);
        }
        io_error_ = true;
        if (io_error_message_.empty()) {
          io_error_message_ = failure_message;
        }
        append_diagnostic_locked(failure_message);
        diagnostic = std::move(failure_message);
        ++error_count_;
        force_hresult_failure = true;
      } else if (pending_entry_.has_value()) {
        // Budget: bytes check on successful commit to the final destination.
        if (budget_.has_value() && budget_->max_bytes.has_value() &&
            pending_entry_->declared_size > 0) {
          const uint64_t total_bytes =
              budget_bytes_seen_.fetch_add(pending_entry_->declared_size,
                                           std::memory_order_acq_rel) +
              pending_entry_->declared_size;
          if (total_bytes > *budget_->max_bytes) {
            if (!budget_triggered_.exchange(true, std::memory_order_acq_rel)) {
              budget_trigger_reason_ =
                  "max_bytes limit exceeded (" +
                  std::to_string(*budget_->max_bytes) + ")";
            }
          }
        }

        ExtractMaterializedEntry me;
        me.archive_entry_path = std::move(pending_entry_->archive_entry_path);
        me.absolute_output_path = std::move(pending_entry_->absolute_output_path);
        me.is_directory = false;
        me.bytes_written = pending_entry_->declared_size;
        me.overwrote_existing = pending_entry_->overwrote_existing;
        me.renamed_from_collision = pending_entry_->renamed_from_collision;
        materialized_entries_.push_back(std::move(me));

        ExtractRollbackEntry rollback_entry;
        rollback_entry.output_path = pending_entry_->output_path;
        rollback_entry.destination_path = pending_entry_->destination_path;
        rollback_entry.backup_path = pending_entry_->backup_path;
        rollback_entry.had_original = pending_entry_->had_original;
        rollback_entry.preserve_backup_on_commit =
            pending_entry_->preserve_backup_on_commit;
        rollback_entry.is_directory = false;
        rollback_entries_.push_back(std::move(rollback_entry));
      }
      pending_entry_.reset();
    } else {
      std::string close_error_message;
      if (pending_entry_.has_value() &&
          !close_pending_entry_stream_locked(*pending_entry_,
                                             &close_error_message)) {
        io_error_ = true;
        if (io_error_message_.empty()) {
          io_error_message_ = close_error_message;
        }
        append_diagnostic_locked(close_error_message);
      }

      std::string restore_error_message;
      if (pending_entry_.has_value() &&
          !restore_pending_entry_original_locked(*pending_entry_, &restore_error_message)) {
        io_error_ = true;
        if (io_error_message_.empty()) {
          io_error_message_ = restore_error_message;
        }
        append_diagnostic_locked(restore_error_message);
      }
      pending_entry_.reset();
      ++error_count_;
      if (op_res == NArchive::NExtract::NOperationResult::kWrongPassword ||
          (encrypted_item &&
           (op_res == NArchive::NExtract::NOperationResult::kDataError ||
            op_res == NArchive::NExtract::NOperationResult::kCRCError ||
            op_res == NArchive::NExtract::NOperationResult::kHeadersError))) {
        wrong_password_ = true;
      }
      diagnostic = test_operation_result_message(op_res);
      if (!path.empty()) {
        diagnostic = path + " : " + diagnostic;
      }
      if (!diagnostic.empty()) {
        append_diagnostic_locked(diagnostic);
      }
    }
  }

  if (zone_identifier_target.has_value()) {
    apply_zone_identifier_to_file(*zone_identifier_target);
  }

  if (op_res != NArchive::NExtract::NOperationResult::kOK ||
      force_hresult_failure) {
    std::string message = std::move(diagnostic);
    if (message.empty()) {
      message = test_operation_result_message(op_res);
      if (!path.empty()) {
        message = path + " : " + message;
      }
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kStdErr,
                   message);
  }

  emit_progress_snapshot();
  if (force_hresult_failure) {
    return E_FAIL;
  }
  return check_canceled();
}

}  // namespace z7::app
