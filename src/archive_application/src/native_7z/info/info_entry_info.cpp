// src/archive_application/src/native_7z/info/info_entry_info.cpp
// Role: GetEntryInfo – single archive-entry metadata query (B2).

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "session/session_registry_internal.h"

namespace z7::app {

namespace {

// Accumulate subtree statistics for entries whose path begins with `prefix`.
void compute_subtree_stats(IInArchive* archive,
                           UInt32 num_items,
                           const std::string& prefix,
                           GetEntryInfoResult& result) {
  uint64_t file_count = 0;
  uint64_t total_size = 0;
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string item_path =
        normalize_archive_item_path(archive_get_prop_text(archive, i, kpidPath));
    if (item_path.empty()) {
      continue;
    }
    // Skip the directory entry itself (exact match with prefix stripped of trailing '/').
    if (!prefix.empty() && item_path.size() < prefix.size()) {
      continue;
    }
    const bool is_child =
        prefix.empty() ||
        (item_path.size() >= prefix.size() &&
         item_path.compare(0, prefix.size(), prefix) == 0);
    if (!is_child) {
      continue;
    }
    bool is_dir = false;
    (void)archive_get_prop_bool(archive, i, kpidIsDir, is_dir);
    if (!is_dir) {
      ++file_count;
      uint64_t sz = 0;
      if (archive_get_prop_uint64(archive, i, kpidSize, sz)) {
        total_size += sz;
      }
    }
  }
  result.subtree_file_count = file_count;
  result.subtree_total_size = total_size;
}

GetEntryInfoResult resolve_entry_info_from_arc(IInArchive* archive,
                                               UInt32 num_items,
                                               const std::string& raw_entry_path) {
  const std::string norm = normalize_archive_item_path(raw_entry_path);

  GetEntryInfoResult result;
  result.ok = true;

  // Empty / root query — archive root always exists as a virtual directory.
  if (norm.empty()) {
    result.exists = true;
    result.is_directory = true;
    compute_subtree_stats(archive, num_items, "", result);
    return result;
  }

  // First pass: look for an exact path match in the archive index.
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string item_path =
        normalize_archive_item_path(archive_get_prop_text(archive, i, kpidPath));
    if (item_path != norm) {
      continue;
    }
    result.exists = true;
    (void)archive_get_prop_bool(archive, i, kpidIsDir, result.is_directory);
    (void)archive_get_prop_uint64(archive, i, kpidSize, result.size);

    int64_t t = 0;
    if (archive_get_prop_time_msecs_utc(archive, i, kpidMTime, t)) {
      result.mtime_msecs_utc = t;
    }
    if (archive_get_prop_time_msecs_utc(archive, i, kpidCTime, t)) {
      result.ctime_msecs_utc = t;
    }
    if (archive_get_prop_time_msecs_utc(archive, i, kpidATime, t)) {
      result.atime_msecs_utc = t;
    }
    bool enc = false;
    if (archive_get_prop_bool(archive, i, kpidEncrypted, enc)) {
      result.encrypted = enc;
    }
    uint32_t crc32 = 0;
    if (archive_get_prop_uint32(archive, i, kpidCRC, crc32)) {
      result.crc = crc32;
    }

    if (result.is_directory) {
      compute_subtree_stats(archive, num_items, norm + '/', result);
    }
    return result;
  }

  // Second pass: some formats (zip, gzip-wrapped tar) don't store explicit
  // directory entries. If any child path has norm+'/' as a prefix, the
  // directory implicitly exists.
  const std::string prefix = norm + '/';
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string item_path =
        normalize_archive_item_path(archive_get_prop_text(archive, i, kpidPath));
    if (item_path.size() > prefix.size() &&
        item_path.compare(0, prefix.size(), prefix) == 0) {
      result.exists = true;
      result.is_directory = true;
      compute_subtree_stats(archive, num_items, prefix, result);
      return result;
    }
  }

  // exists = false; ok = true.
  return result;
}

}  // namespace

GetEntryInfoResult NativeArchiveBackend::get_entry_info(
    const GetEntryInfoRequest& request,
    const ArchiveBackendHooks& hooks) {
  // Session-reuse path: open archive is already cached.
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session =
        ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<GetEntryInfoResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    const CArc* arc = archive_session_link(*session).GetArc();
    if (arc == nullptr || arc->Archive == nullptr) {
      return make_operation_failure<GetEntryInfoResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Session archive unavailable",
          7);
    }
    UInt32 num_items = 0;
    if (arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
      return make_operation_failure<GetEntryInfoResult>(
          ArchiveErrorDomain::kUnknown,
          "GetNumberOfItems failed",
          2);
    }
    return resolve_entry_info_from_arc(arc->Archive, num_items, request.entry_path);
  }

  // Direct-open path: open the archive once, query, close.
  return run_open_archive_read_pipeline<GetEntryInfoResult>(
      request.archive_path,
      request.archive_type_hint,
      hooks,
      false,
      [&](const OpenArchiveReadState& open_state, UInt32 num_items) -> GetEntryInfoResult {
        return resolve_entry_info_from_arc(
            open_state.arc->Archive, num_items, request.entry_path);
      });
}

}  // namespace z7::app
