#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "archive_types_base.h"

namespace z7::app {

enum class PropertyLineKind {
  kPair,
  kSeparator,
  kSeparatorSmall
};

enum class ArchivePropertySection {
  kNone,
  kSelectedItem,
  kSelectedItemRaw,
  kSelectionSummary,
  kCurrentFolder,
  kArchiveProps,
  kArchiveProps2,
  kNonOpenError
};

enum class ArchivePropertyDisplayGroup {
  kNone,
  kSelectedItemProperties,
  kSelectedItemRawProperties,
  kSelectionSummary,
  kCurrentFolderPath,
  kCurrentFolderProperties,
  kArchiveProperties,
  kArchiveProperties2,
  kNonOpenError
};

struct ArchivePropertyLine {
  PropertyLineKind kind = PropertyLineKind::kPair;
  ArchivePropertySection section = ArchivePropertySection::kNone;
  ArchivePropertyDisplayGroup display_group = ArchivePropertyDisplayGroup::kNone;
  std::optional<uint32_t> level;
  std::optional<uint32_t> prop_id;
  std::string name;
  std::string value;
};

struct ArchivePropertiesRequest {
  std::string archive_path;
  // UTF-8 paths inside archive, relative to archive root.
  std::vector<std::string> entries;
  // UTF-8 virtual directory where selection is made.
  std::string directory;
  bool flat_view = false;
  // Optional hint such as "*" / "#" / "7z" (same syntax as original arcFormat).
  std::string archive_type_hint;
  std::optional<ArchiveSessionToken> session_token;
};

}  // namespace z7::app
