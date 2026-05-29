// src/archive_application/src/native_7z/core/core_archive.cpp
// Role: Archive-virtual path and property extraction helpers.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

std::string normalize_archive_virtual_directory(std::string directory) {
  std::replace(directory.begin(), directory.end(), '\\', '/');

  std::string out;
  out.reserve(directory.size());
  bool last_was_slash = false;
  for (char ch : directory) {
    if (ch == '/') {
      if (out.empty() || last_was_slash) {
        last_was_slash = true;
        continue;
      }
      out.push_back(ch);
      last_was_slash = true;
    } else {
      out.push_back(ch);
      last_was_slash = false;
    }
  }

  while (!out.empty() && out.front() == '/') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '/') {
    out.pop_back();
  }
  return out;
}

std::vector<std::string> split_archive_virtual_directory(const std::string& directory) {
  std::vector<std::string> parts;
  if (directory.empty()) {
    return parts;
  }

  size_t start = 0;
  while (start < directory.size()) {
    const size_t slash = directory.find('/', start);
    const std::string token = directory.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!token.empty()) {
      parts.push_back(token);
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return parts;
}

bool archive_virtual_path_is_safe_for_materialization(
    const std::string& normalized_path) {
  for (const std::string& part :
       split_archive_virtual_directory(normalized_path)) {
    if (part == "." || part == "..") {
      return false;
    }
  }
  return true;
}

bool archive_get_prop_uint64(IInArchive* archive,
                             UInt32 index,
                             PROPID prop_id,
                             uint64_t& value) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, prop_id, &prop) != S_OK) {
    return false;
  }

  UInt64 converted = 0;
  if (!ConvertPropVariantToUInt64(prop, converted)) {
    return false;
  }
  value = converted;
  return true;
}

bool archive_get_prop_uint32(IInArchive* archive,
                             UInt32 index,
                             PROPID prop_id,
                             uint32_t& value) {
  uint64_t converted = 0;
  if (!archive_get_prop_uint64(archive, index, prop_id, converted) ||
      converted > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  value = static_cast<uint32_t>(converted);
  return true;
}

bool archive_get_prop_bool(IInArchive* archive,
                           UInt32 index,
                           PROPID prop_id,
                           bool& value) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, prop_id, &prop) != S_OK) {
    return false;
  }

  if (prop.vt == VT_BOOL) {
    value = (prop.boolVal != VARIANT_FALSE);
    return true;
  }

  uint64_t converted = 0;
  if (ConvertPropVariantToUInt64(prop, converted)) {
    value = (converted != 0);
    return true;
  }
  return false;
}

bool archive_get_prop_time_msecs_utc(IInArchive* archive,
                                     UInt32 index,
                                     PROPID prop_id,
                                     int64_t& value) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, prop_id, &prop) != S_OK ||
      prop.vt != VT_FILETIME) {
    return false;
  }

  UInt32 quantums = 0;
  const Int64 seconds = NWindows::NTime::FileTime_To_UnixTime64_and_Quantums(
      prop.filetime,
      quantums);
  value = seconds * 1000 + static_cast<int64_t>(quantums / 10000);
  return true;
}

std::string archive_get_prop_text(IInArchive* archive,
                                  UInt32 index,
                                  PROPID prop_id) {
  NWindows::NCOM::CPropVariant prop;
  if (archive->GetProperty(index, prop_id, &prop) != S_OK ||
      prop.vt == VT_EMPTY) {
    return {};
  }

  UString converted;
  ConvertPropertyToString2(converted, prop, prop_id);
  return ustring_to_utf8(converted);
}

void fill_archive_list_entry_details(IInArchive* archive,
                                     UInt32 arc_index,
                                     ArchiveListEntry& entry) {
  uint64_t value64 = 0;
  uint32_t value32 = 0;
  bool value_bool = false;
  int64_t value_time = 0;

  if (archive_get_prop_uint64(archive, arc_index, kpidPackSize, value64)) {
    entry.packed_size = value64;
  }
  if (archive_get_prop_time_msecs_utc(archive, arc_index, kpidMTime, value_time)) {
    entry.mtime_msecs_utc = value_time;
  }
  if (archive_get_prop_time_msecs_utc(archive, arc_index, kpidCTime, value_time)) {
    entry.ctime_msecs_utc = value_time;
  }
  if (archive_get_prop_time_msecs_utc(archive, arc_index, kpidATime, value_time)) {
    entry.atime_msecs_utc = value_time;
  }

  entry.attributes = archive_get_prop_text(archive, arc_index, kpidAttrib);
  if (archive_get_prop_bool(archive, arc_index, kpidEncrypted, value_bool)) {
    entry.encrypted = value_bool;
  }
  entry.comment = archive_get_prop_text(archive, arc_index, kpidComment);
  if (archive_get_prop_uint32(archive, arc_index, kpidCRC, value32)) {
    entry.crc = value32;
  }
  entry.method = archive_get_prop_text(archive, arc_index, kpidMethod);
  entry.characts = archive_get_prop_text(archive, arc_index, kpidCharacts);
  entry.host_os = archive_get_prop_text(archive, arc_index, kpidHostOS);
  entry.version = archive_get_prop_text(archive, arc_index, kpidUnpackVer);
  if (archive_get_prop_uint64(archive, arc_index, kpidVolumeIndex, value64)) {
    entry.volume_index = value64;
  }
  if (archive_get_prop_uint64(archive, arc_index, kpidOffset, value64)) {
    entry.offset = value64;
  }
  if (archive_get_prop_uint64(archive, arc_index, kpidNumSubDirs, value64)) {
    entry.num_sub_dirs = value64;
  }
  if (archive_get_prop_uint64(archive, arc_index, kpidNumSubFiles, value64)) {
    entry.num_sub_files = value64;
  }
}

void fill_proxy_dir_stats(const CProxyDir& dir, ArchiveListEntry& entry) {
  if (!entry.packed_size.has_value()) {
    entry.packed_size = dir.PackSize;
  }
  if (!entry.num_sub_dirs.has_value()) {
    entry.num_sub_dirs = dir.NumSubDirs;
  }
  if (!entry.num_sub_files.has_value()) {
    entry.num_sub_files = dir.NumSubFiles;
  }
  if (!entry.crc.has_value() && dir.CrcIsDefined) {
    entry.crc = dir.Crc;
  }
}

void fill_proxy_dir2_stats(const CProxyDir2& dir, ArchiveListEntry& entry) {
  if (!entry.packed_size.has_value()) {
    entry.packed_size = dir.PackSize;
  }
  if (!entry.num_sub_dirs.has_value()) {
    entry.num_sub_dirs = dir.NumSubDirs;
  }
  if (!entry.num_sub_files.has_value()) {
    entry.num_sub_files = dir.NumSubFiles;
  }
  if (!entry.crc.has_value() && dir.CrcIsDefined) {
    entry.crc = dir.Crc;
  }
}

std::string test_operation_result_message(Int32 op_res) {
  switch (op_res) {
    case NArchive::NExtract::NOperationResult::kOK:
      return {};
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
      return "Unsupported method";
    case NArchive::NExtract::NOperationResult::kDataError:
      return "Data error";
    case NArchive::NExtract::NOperationResult::kCRCError:
      return "CRC failed";
    case NArchive::NExtract::NOperationResult::kUnavailable:
      return "Unavailable data";
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
      return "Unexpected end of data";
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
      return "There are some data after the end of payload data";
    case NArchive::NExtract::NOperationResult::kIsNotArc:
      return "Is not archive";
    case NArchive::NExtract::NOperationResult::kHeadersError:
      return "Headers error";
    case NArchive::NExtract::NOperationResult::kWrongPassword:
      return "Wrong password";
  }
  return "Unknown extract error";
}

}  // namespace z7::app
