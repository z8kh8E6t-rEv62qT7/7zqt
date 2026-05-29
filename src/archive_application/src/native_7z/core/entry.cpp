// src/archive_application/src/native_7z/core/entry.cpp
// Role: Archive entry selection/stats and update-message helpers.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

TestArchiveItemStats collect_test_archive_item_stats(IInArchive* archive,
                                                     UInt32 num_items) {
  TestArchiveItemStats stats;
  if (archive == nullptr) {
    return stats;
  }

  for (UInt32 i = 0; i < num_items; ++i) {
    bool is_dir = false;
    if (archive_get_prop_bool(archive, i, kpidIsDir, is_dir) && is_dir) {
      ++stats.num_dirs;
      continue;
    }

    ++stats.num_files;
    uint64_t item_size = 0;
    if (archive_get_prop_uint64(archive, i, kpidSize, item_size)) {
      stats.total_unpacked_size += item_size;
    }
  }

  return stats;
}

void accumulate_test_item_stats(IInArchive* archive,
                                UInt32 index,
                                TestArchiveItemStats& stats) {
  if (archive == nullptr) {
    return;
  }

  bool is_dir = false;
  if (archive_get_prop_bool(archive, index, kpidIsDir, is_dir) && is_dir) {
    ++stats.num_dirs;
    return;
  }

  ++stats.num_files;
  uint64_t item_size = 0;
  if (archive_get_prop_uint64(archive, index, kpidSize, item_size)) {
    stats.total_unpacked_size += item_size;
  }
}

std::string normalize_archive_item_path(const std::string& value) {
  return normalize_archive_virtual_directory(value);
}

std::string archive_item_selection_path(IInArchive* archive, UInt32 index) {
  if (archive == nullptr) {
    return {};
  }

  const std::string item_path =
      normalize_archive_item_path(archive_get_prop_text(archive, index, kpidPath));
  if (!item_path.empty()) {
    return item_path;
  }

  bool is_dir = false;
  if (archive_get_prop_bool(archive, index, kpidIsDir, is_dir) && is_dir) {
    return {};
  }
  return std::to_string(index);
}

bool archive_path_matches_selection(const std::string& item_path,
                                    const std::unordered_set<std::string>& selected_entries) {
  if (selected_entries.empty()) {
    return true;
  }
  if (selected_entries.find(item_path) != selected_entries.end()) {
    return true;
  }

  for (const std::string& selected : selected_entries) {
    if (selected.empty() || item_path.size() <= selected.size()) {
      continue;
    }
    if (item_path.compare(0, selected.size(), selected) != 0) {
      continue;
    }
    if (item_path[selected.size()] == '/') {
      return true;
    }
  }
  return false;
}

void accumulate_extract_item_stats(IInArchive* archive,
                                   UInt32 index,
                                   ExtractArchiveItemStats& stats) {
  bool is_dir = false;
  if (archive_get_prop_bool(archive, index, kpidIsDir, is_dir) && is_dir) {
    ++stats.num_dirs;
    return;
  }

  ++stats.num_files;
  uint64_t item_size = 0;
  if (archive_get_prop_uint64(archive, index, kpidSize, item_size)) {
    stats.total_unpacked_size += item_size;
  }
}

fs::path make_unique_destination_path(const fs::path& original_path,
                                      std::error_code& ec) {
  ec.clear();
  std::error_code exists_ec;
  if (!fs::exists(original_path, exists_ec)) {
    if (exists_ec) {
      ec = exists_ec;
      return {};
    }
    return original_path;
  }
  if (exists_ec) {
    ec = exists_ec;
    return {};
  }

  const fs::path parent = original_path.parent_path();
  const std::string stem = original_path.stem().string();
  const std::string ext = original_path.extension().string();
  for (uint64_t suffix = 1; suffix < 10000; ++suffix) {
    const fs::path candidate =
        parent / fs::path(stem + "_" + std::to_string(suffix) + ext);
    exists_ec.clear();
    if (!fs::exists(candidate, exists_ec)) {
      if (exists_ec) {
        ec = exists_ec;
        return {};
      }
      return candidate;
    }
    if (exists_ec) {
      ec = exists_ec;
      return {};
    }
  }

  const uint64_t unique_suffix = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path candidate =
      parent / fs::path(stem + "_" + std::to_string(unique_suffix) + ext);
  exists_ec.clear();
  if (!fs::exists(candidate, exists_ec)) {
    if (exists_ec) {
      ec = exists_ec;
      return {};
    }
    return candidate;
  }
  if (exists_ec) {
    ec = exists_ec;
  } else {
    ec = std::make_error_code(std::errc::file_exists);
  }
  return {};
}

std::string update_wide_name_to_utf8(const wchar_t* name) {
  if (name == nullptr) {
    return {};
  }
  return ustring_to_utf8(UString(name));
}

std::string update_error_message_to_utf8(const CUpdateErrorInfo& error_info) {
  std::string message = astring_to_std(error_info.Message);
  if (!error_info.FileNames.IsEmpty()) {
    if (!message.empty()) {
      message += " : ";
    }
    message += ustring_to_utf8(fs2us(error_info.FileNames.Front()));
  }
  return message;
}

}  // namespace z7::app

// End of entry.cpp
