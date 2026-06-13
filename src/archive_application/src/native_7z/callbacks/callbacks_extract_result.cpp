// src/archive_application/src/native_7z/callbacks/callbacks_extract_result.cpp
// Role: Extract callback operation result accounting.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"
#include "Windows/FileDir.h"
#include "Windows/FileIO.h"

#include <cerrno>
#include <fstream>
#include <iterator>

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif

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

#endif

#if (defined(_WIN32) && !defined(UNDER_CE)) || defined(__APPLE__)

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

#endif

#if defined(_WIN32) && !defined(UNDER_CE)

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

#if defined(__APPLE__)

constexpr const char* kMacQuarantineAttributeName = "com.apple.quarantine";

std::string read_quarantine_xattr(const fs::path& base_path) {
  const std::string native_path = base_path.string();
  errno = 0;
  const ssize_t size =
      ::getxattr(native_path.c_str(),
                 kMacQuarantineAttributeName,
                 nullptr,
                 0,
                 0,
                 XATTR_NOFOLLOW);
  if (size <= 0) {
    return {};
  }

  std::string data(static_cast<size_t>(size), '\0');
  errno = 0;
  const ssize_t actual_size =
      ::getxattr(native_path.c_str(),
                 kMacQuarantineAttributeName,
                 data.data(),
                 data.size(),
                 0,
                 XATTR_NOFOLLOW);
  if (actual_size <= 0) {
    return {};
  }
  data.resize(static_cast<size_t>(actual_size));
  return data;
}

void write_quarantine_xattr(const fs::path& output_path,
                            const std::string& quarantine_data) {
  if (quarantine_data.empty()) {
    return;
  }
  const std::string native_path = output_path.string();
  (void)::setxattr(native_path.c_str(),
                   kMacQuarantineAttributeName,
                   quarantine_data.data(),
                   quarantine_data.size(),
                   0,
                   XATTR_NOFOLLOW);
}

#endif

FString filesystem_path_to_fstring(const fs::path& path) {
  return us2fs(utf8_to_ustring(path.string()));
}

std::string normalize_link_target_separators(std::string target) {
  std::replace(target.begin(), target.end(), '\\', '/');
  std::string out;
  out.reserve(target.size());
  bool last_was_slash = false;
  for (char ch : target) {
    if (ch == '/') {
      if (last_was_slash) {
        continue;
      }
      last_was_slash = true;
    } else {
      last_was_slash = false;
    }
    out.push_back(ch);
  }
  return out;
}

bool path_is_within_root(const fs::path& candidate, const fs::path& root) {
  const std::string root_text =
      fs::absolute(root).lexically_normal().generic_string();
  const std::string candidate_text =
      fs::absolute(candidate).lexically_normal().generic_string();
  if (root_text == "/") {
    return !candidate_text.empty() && candidate_text.front() == '/';
  }
  if (candidate_text == root_text) {
    return true;
  }
  return candidate_text.size() > root_text.size() &&
         candidate_text.compare(0, root_text.size(), root_text) == 0 &&
         candidate_text[root_text.size()] == '/';
}

std::optional<std::string> normalize_archive_relative_link_target(
    const std::string& target) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= target.size()) {
    const size_t slash = target.find('/', start);
    const std::string token =
        target.substr(start,
                      slash == std::string::npos ? std::string::npos
                                                  : slash - start);
    if (!token.empty() && token != ".") {
      if (token == "..") {
        if (parts.empty()) {
          return std::nullopt;
        }
        parts.pop_back();
      } else {
        parts.push_back(token);
      }
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }

  std::string normalized;
  for (const std::string& part : parts) {
    if (!normalized.empty()) {
      normalized.push_back('/');
    }
    normalized += part;
  }
  if (normalized.empty()) {
    return std::nullopt;
  }
  return normalized;
}

enum class HardLinkTargetState {
  kReady,
  kMissing,
  kInvalid
};

HardLinkTargetState inspect_hard_link_target(const fs::path& target_path,
                                             std::string& warning) {
  warning.clear();
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(target_path, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    return HardLinkTargetState::kMissing;
  }
  if (ec) {
    warning = "Cannot query hard link target: " +
              target_path.generic_string() + "; " + ec.message();
    return HardLinkTargetState::kInvalid;
  }
  if (!fs::status_known(status) || status.type() == fs::file_type::not_found) {
    return HardLinkTargetState::kMissing;
  }
  if (fs::is_symlink(status)) {
    warning = "Hard link target is a symbolic link and was skipped: " +
              target_path.generic_string();
    return HardLinkTargetState::kInvalid;
  }
  if (!fs::is_regular_file(status)) {
    warning = "Hard link target is not a regular file and was skipped: " +
              target_path.generic_string();
    return HardLinkTargetState::kInvalid;
  }
  return HardLinkTargetState::kReady;
}

}  // namespace

void NativeExtractCallback::record_nonfatal_warning(const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!diagnostic_message_.empty()) {
      diagnostic_message_ += '\n';
    }
    diagnostic_message_ += message;
  }
  emit_log_event(hooks_,
                 OperationStage::kRunning,
                 OutputChannel::kStdErr,
                 message);
}

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
        pending_entry.output_path.generic_string();
  }
  return false;
}

HRESULT NativeExtractCallback::read_item_attributes(
    UInt32 index,
    ExtractItemAttributes& attributes) const {
  attributes = ExtractItemAttributes{};

  {
    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive_->GetProperty(index, kpidPosixAttrib, &prop);
    if (hr != S_OK) {
      return hr;
    }
    if (prop.vt == VT_UI4) {
      attributes.defined = true;
      attributes.attrib =
          static_cast<UInt32>((prop.ulVal << 16) | FILE_ATTRIBUTE_UNIX_EXTENSION);
    } else if (prop.vt != VT_EMPTY) {
      return E_FAIL;
    }
  }

  {
    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive_->GetProperty(index, kpidAttrib, &prop);
    if (hr != S_OK) {
      return hr;
    }
    if (prop.vt == VT_UI4) {
      attributes.defined = true;
      attributes.attrib = prop.ulVal;
    } else if (prop.vt != VT_EMPTY) {
      return E_FAIL;
    }
  }

  return S_OK;
}

HRESULT NativeExtractCallback::read_item_link_info(
    UInt32 index,
    ExtractItemLinkInfo& link_info) const {
  link_info = ExtractItemLinkInfo{};

  {
    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive_->GetProperty(index, kpidHardLink, &prop);
    if (hr != S_OK) {
      return hr;
    }
    if (prop.vt == VT_BSTR) {
      UString target;
      target.SetFromBstr(prop.bstrVal);
      link_info.type = ExtractItemLinkInfo::Type::kHardLink;
      link_info.target = ustring_to_utf8(target);
    } else if (prop.vt != VT_EMPTY) {
      return E_FAIL;
    }
  }

  {
    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive_->GetProperty(index, kpidSymLink, &prop);
    if (hr != S_OK) {
      return hr;
    }
    if (prop.vt == VT_BSTR) {
      UString target;
      target.SetFromBstr(prop.bstrVal);
      link_info.type = ExtractItemLinkInfo::Type::kSymLink;
      link_info.target = ustring_to_utf8(target);
    } else if (prop.vt != VT_EMPTY) {
      return E_FAIL;
    }
  }

  return S_OK;
}

std::optional<std::string> NativeExtractCallback::apply_item_attributes(
    const fs::path& output_path,
    const ExtractItemAttributes& attributes) const {
  if (!attributes.defined) {
    return std::nullopt;
  }

  const FString native_path = filesystem_path_to_fstring(output_path);
  if (NWindows::NFile::NDir::SetFileAttrib_PosixHighDetect(
          native_path, attributes.attrib)) {
    return std::nullopt;
  }
  return "Cannot set file attribute: " + output_path.generic_string();
}

std::optional<std::string> NativeExtractCallback::prepare_link_creation_plan(
    const OutputTarget& output_target,
    const ExtractItemLinkInfo& link_info,
    LinkCreationPlan& plan) const {
  plan = LinkCreationPlan{};
  std::string target = normalize_link_target_separators(link_info.target);
  if (target.empty()) {
    return "Empty link target was skipped: " + output_target.archive_entry_path;
  }
  if (is_absolute_item_path(target)) {
    return "Unsafe absolute link target was skipped: " +
           output_target.archive_entry_path + " -> " + target;
  }

  if (link_info.type == ExtractItemLinkInfo::Type::kSymLink) {
    const fs::path resolved_target =
        (output_target.output_path.parent_path() / fs::path(target))
            .lexically_normal();
    if (!path_is_within_root(resolved_target, output_dir_)) {
      return "Unsafe symbolic link target was skipped: " +
             output_target.archive_entry_path + " -> " + target;
    }
    plan.type = ExtractItemLinkInfo::Type::kSymLink;
    plan.symlink_target = std::move(target);
    return std::nullopt;
  }

  if (link_info.type == ExtractItemLinkInfo::Type::kHardLink) {
    const std::optional<std::string> normalized_target =
        normalize_archive_relative_link_target(target);
    if (!normalized_target.has_value() ||
        !archive_virtual_path_is_safe_for_materialization(*normalized_target)) {
      return "Unsafe hard link target was skipped: " +
             output_target.archive_entry_path + " -> " + target;
    }

    const ResolvedPath resolved_target =
        resolve_destination_path(*normalized_target);
    if (!path_is_within_root(resolved_target.destination_path, output_dir_)) {
      return "Hard link target outside extraction root was skipped: " +
             output_target.archive_entry_path + " -> " + *normalized_target;
    }

    plan.type = ExtractItemLinkInfo::Type::kHardLink;
    plan.hardlink_target_path = resolved_target.destination_path;
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<std::string> NativeExtractCallback::create_symbolic_link(
    const fs::path& output_path,
    const std::string& target) const {
#if defined(_WIN32)
  (void)output_path;
  (void)target;
  return "Symbolic link extraction is not supported on this platform.";
#else
  const FString native_output_path = filesystem_path_to_fstring(output_path);
  if (NWindows::NFile::NIO::SetSymLink_UString(native_output_path,
                                               utf8_to_ustring(target))) {
    return std::nullopt;
  }
  return "Cannot create symbolic link: " + output_path.generic_string() +
         " -> " + target;
#endif
}

std::optional<std::string> NativeExtractCallback::create_hard_link(
    const fs::path& output_path,
    const fs::path& target_path) const {
#if defined(_WIN32)
  (void)output_path;
  (void)target_path;
  return "Hard link extraction is not supported on this platform.";
#else
  if (NWindows::NFile::NDir::MyCreateHardLink(
          filesystem_path_to_fstring(output_path),
          filesystem_path_to_fstring(target_path))) {
    return std::nullopt;
  }
  return "Cannot create hard link: " + output_path.generic_string() +
         " -> " + target_path.generic_string();
#endif
}

void NativeExtractCallback::record_materialized_output_locked(
    const OutputTarget& target,
    uint64_t bytes_written,
    bool is_directory) {
  ExtractMaterializedEntry materialized_entry;
  materialized_entry.archive_entry_path = target.archive_entry_path;
  materialized_entry.absolute_output_path = target.absolute_output_path;
  materialized_entry.is_directory = is_directory;
  materialized_entry.bytes_written = bytes_written;
  materialized_entry.overwrote_existing = target.overwrote_existing;
  materialized_entry.renamed_from_collision = target.renamed_from_collision;
  materialized_entries_.push_back(std::move(materialized_entry));

  ExtractRollbackEntry rollback_entry;
  rollback_entry.output_path = target.output_path;
  rollback_entry.destination_path = target.destination_path;
  rollback_entry.backup_path = target.backup_path;
  rollback_entry.had_original = target.had_original;
  rollback_entry.preserve_backup_on_commit = target.preserve_backup_on_commit;
  rollback_entry.is_directory = is_directory;
  rollback_entries_.push_back(std::move(rollback_entry));
}

std::optional<std::string> NativeExtractCallback::materialize_link(
    const OutputTarget& output_target,
    const LinkCreationPlan& plan,
    bool allow_defer) {
  if (plan.type == ExtractItemLinkInfo::Type::kSymLink) {
    std::optional<std::string> warning =
        create_symbolic_link(output_target.output_path, plan.symlink_target);
    if (warning.has_value()) {
      return warning;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    record_materialized_output_locked(output_target, 0, false);
    return std::nullopt;
  }

  if (plan.type == ExtractItemLinkInfo::Type::kHardLink) {
    std::string target_warning;
    const HardLinkTargetState target_state =
        inspect_hard_link_target(plan.hardlink_target_path, target_warning);
    if (target_state == HardLinkTargetState::kMissing) {
      if (allow_defer) {
        std::lock_guard<std::mutex> lock(mutex_);
        DeferredHardLink deferred;
        deferred.output_target = output_target;
        deferred.target_path = plan.hardlink_target_path;
        deferred_hard_links_.push_back(std::move(deferred));
        return std::nullopt;
      }
      return "Hard link target is missing and was skipped: " +
             output_target.archive_entry_path + " -> " +
             plan.hardlink_target_path.generic_string();
    }
    if (target_state == HardLinkTargetState::kInvalid) {
      return target_warning;
    }

    std::optional<std::string> warning =
        create_hard_link(output_target.output_path, plan.hardlink_target_path);
    if (warning.has_value()) {
      return warning;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    record_materialized_output_locked(output_target, 0, false);
  }
  return std::nullopt;
}

void NativeExtractCallback::finish_deferred_links() {
  std::vector<DeferredHardLink> deferred_links;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    deferred_links = std::move(deferred_hard_links_);
    deferred_hard_links_.clear();
  }

  for (const DeferredHardLink& deferred : deferred_links) {
    LinkCreationPlan plan;
    plan.type = ExtractItemLinkInfo::Type::kHardLink;
    plan.hardlink_target_path = deferred.target_path;
    if (const std::optional<std::string> warning =
            materialize_link(deferred.output_target, plan, false);
        warning.has_value()) {
      record_nonfatal_warning(*warning);
    }
  }
}

void NativeExtractCallback::apply_zone_identifier_to_output(
    const fs::path& output_path,
    bool is_directory) const {
#if defined(_WIN32) && !defined(UNDER_CE)
  if (is_directory) {
    return;
  }
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
#elif defined(__APPLE__)
  if (zone_id_mode_ == ExtractZoneIdMode::kNone || archive_path_.empty()) {
    return;
  }
  if (is_directory && zone_id_mode_ != ExtractZoneIdMode::kAll) {
    return;
  }
  if (!is_directory && zone_id_mode_ == ExtractZoneIdMode::kOffice &&
      !is_office_document_path(output_path)) {
    return;
  }

  const std::string quarantine_data =
      read_quarantine_xattr(fs::path(archive_path_));
  write_quarantine_xattr(output_path, quarantine_data);
#else
  (void)output_path;
  (void)is_directory;
#endif
}

STDMETHODIMP NativeExtractCallback::SetOperationResult(Int32 op_res) throw() {
  std::string path;
  std::string diagnostic;
  bool force_hresult_failure = false;
  bool encrypted_item = false;
  std::optional<fs::path> zone_identifier_target;
  std::optional<std::string> attribute_warning;
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
          attribute_warning = apply_item_attributes(pending_entry_->output_path,
                                                    pending_entry_->attributes);
          if (attribute_warning.has_value()) {
            append_diagnostic_locked(*attribute_warning);
          }
          zone_identifier_target = pending_entry_->output_path;
        }
      }

      if (!failure_message.empty()) {
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
    apply_zone_identifier_to_output(*zone_identifier_target, false);
  }
  if (attribute_warning.has_value()) {
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kStdErr,
                   *attribute_warning);
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
