#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "archive_types_base.h"

namespace z7::app {

enum class ExtractPathMode {
  kFullPaths,
  kNoPaths,
  kAbsolutePaths
};

enum class ExtractZoneIdMode {
  kNone,
  kAll,
  kOffice
};

enum class ExtractPathRemapMatchKind {
  kRequestRoot,
  kExactArchivePath,
  kArchivePrefix
};

enum class OverwriteDecision {
  kYes,
  kYesToAll,
  kNo,
  kNoToAll,
  kAutoRename,
  kCancel
};

struct OverwritePrompt {
  std::string existing_path;
  std::string incoming_path;
  bool existing_size_defined = false;
  uint64_t existing_size = 0;
  bool incoming_size_defined = false;
  uint64_t incoming_size = 0;
  std::optional<int64_t> existing_mtime_msecs_utc;
  std::optional<int64_t> incoming_mtime_msecs_utc;
};

using AskOverwriteCallback = std::function<OverwriteDecision(const OverwritePrompt&)>;

enum class BudgetExceededAction {
  kFailAndRollback,    // stop, delete all extracted files, ok=false
  kFailAndKeepPartial, // stop, keep extracted files so far, ok=false
  kTruncate            // stop, keep extracted files so far, ok=true (partial success)
};

struct ExtractBudget {
  std::optional<uint64_t> max_files;  // max entries (files + dirs) to extract
  std::optional<uint64_t> max_bytes;  // max total declared unpacked bytes
  BudgetExceededAction on_exceeded = BudgetExceededAction::kFailAndRollback;
};

struct ExtractPathRemap {
  ExtractPathRemapMatchKind match_kind = ExtractPathRemapMatchKind::kRequestRoot;
  std::string source_path;
  std::string destination_path;
};

struct ExtractRequest {
  std::string archive_path;
  // Optional multi-archive extraction source list.
  std::vector<std::string> archive_paths;
  std::string output_dir;
  std::string archive_type_hint;
  std::optional<ArchiveSessionToken> session_token;
  OverwriteMode overwrite_mode = OverwriteMode::kAsk;
  ExtractPathMode path_mode = ExtractPathMode::kFullPaths;
  bool eliminate_root_duplication = false;
  ExtractZoneIdMode zone_id_mode = ExtractZoneIdMode::kNone;
  std::string password;
  bool restore_file_security = false;
  uint64_t configured_memory_limit_bytes = 0;
  bool configured_memory_limit_defined = false;
  std::vector<std::string> entries;
  std::vector<ExtractPathRemap> path_remaps;
  // Optional extraction budget. When set, backend stops early if the total
  // number of extracted entries or total declared bytes would exceed the limit.
  // Default nullopt = no budget, behaviour identical to previous semantics.
  std::optional<ExtractBudget> budget;
};

}  // namespace z7::app
