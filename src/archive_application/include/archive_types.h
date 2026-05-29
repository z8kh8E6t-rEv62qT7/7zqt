#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "archive_types_base.h"
#include "archive_types_benchmark.h"
#include "archive_types_extract.h"
#include "archive_types_hash.h"
#include "archive_types_properties.h"

namespace z7::app {

struct ProgressRatioInfo {
  bool input_size_known = false;
  uint64_t input_size = 0;
  bool output_size_known = false;
  uint64_t output_size = 0;
  bool compressing_mode = true;
};

struct OperationEvent {
  OperationEventKind kind = OperationEventKind::kLog;
  OperationStage stage = OperationStage::kRunning;
  OutputChannel output_channel = OutputChannel::kNone;
  std::string message;
  int percent = -1;  // -1 means unknown / indeterminate.
  bool totals_known = false;
  uint64_t total_bytes = 0;
  uint64_t completed_bytes = 0;
  uint64_t total_files = 0;
  uint64_t completed_files = 0;
  uint64_t error_count = 0;
  std::string current_path;
  std::optional<ProgressRatioInfo> ratio_info;
  std::optional<BenchmarkTypedSnapshot> benchmark_snapshot;
  std::optional<BenchmarkTypedSummary> benchmark_summary;
};

struct OperationResult {
  bool ok = false;
  int native_exit_code = 0;
  NativeExecutionInfo native_execution;
  ArchiveError error;
  std::string summary;
  std::optional<HashSummary> hash_summary;
};

struct AddResult : OperationResult {};

// Describes a single filesystem artifact produced by an extract operation.
// Backend populates these from the real write_path chosen inside the 7z
// callback, so callers never need to guess where a file landed.
struct ExtractMaterializedEntry {
  std::string archive_entry_path;   // normalized archive-relative path
  std::string absolute_output_path; // absolute filesystem path actually written
  bool is_directory = false;        // true for created directory entries
  uint64_t bytes_written = 0;       // bytes delivered to disk; 0 for dirs/skip
  bool overwrote_existing = false;  // an existing file at the same path was replaced
  bool renamed_from_collision = false;  // write_path differs from destination_path
};

struct ExtractResult : OperationResult {
  // Ordered by callback delivery (archive index order within a single archive;
  // concatenated across archives when ExtractRequest::archive_paths is used).
  // Excludes entries that were skipped (OverwriteMode::kSkip) or rolled back.
  std::vector<ExtractMaterializedEntry> materialized_entries;

  // Fast-path answer for "ExtractRequest with a single logical entry"
  // (single-file or single-subtree-root). Empty when the request selects
  // multiple entries, spans multiple archives, or nothing was materialized.
  std::string primary_output_path;
  bool primary_is_directory = false;
};

struct TestResult : OperationResult {};

struct BenchmarkResult : OperationResult {
  std::optional<BenchmarkTypedSummary> typed_summary;
  std::string summary_line;
};

struct SplitResult : OperationResult {
  std::string output_path;
  std::vector<std::string> generated_volume_paths;
  uint64_t total_bytes = 0;
  uint64_t volume_count = 0;
};

struct CombineResult : OperationResult {
  std::string output_path;
  std::vector<std::string> input_volume_paths;
  uint64_t total_bytes = 0;
  uint64_t volume_count = 0;
};

struct HashResult : OperationResult {
  HashSummary summary_data;
};

struct DeleteResult : OperationResult {};

struct OpenArchiveResult : OperationResult {
  std::string archive_path;
};

struct OpenArchiveSessionResult : OperationResult {
  enum class Strategy {
    kStream = 1,
    kMemory = 2,
    kTempFile = 3
  };

  ArchiveSessionToken token;
  Strategy used_strategy = Strategy::kTempFile;
  std::string archive_path;
};

struct ListResult : OperationResult {
  std::vector<ArchiveListEntry> entries;
};

struct ArchivePropertiesResult : OperationResult {
  std::vector<ArchivePropertyLine> lines;
};

struct NavigateResult : OperationResult {
  std::string resolved_path;
};

struct CopyResult : OperationResult {
  size_t copied_count = 0;
};

struct MoveResult : OperationResult {
  size_t moved_count = 0;
};

struct RenameResult : OperationResult {
  std::string renamed_path;
};

struct CreateResult : OperationResult {
  std::string created_path;
};

struct ArchiveCommentResult : OperationResult {};

struct FilesystemCommentResult : OperationResult {};

struct GetEntryInfoResult : OperationResult {
  bool exists = false;           // false + ok=true means "query succeeded but entry absent"
  bool is_directory = false;
  uint64_t size = 0;             // unpacked size; 0 for directories
  std::optional<int64_t> mtime_msecs_utc;
  std::optional<int64_t> ctime_msecs_utc;
  std::optional<int64_t> atime_msecs_utc;
  std::optional<bool>    encrypted;
  std::optional<uint32_t> crc;
  // Populated only when is_directory == true; requires an index scan.
  std::optional<uint64_t> subtree_file_count;
  std::optional<uint64_t> subtree_total_size;
};

}  // namespace z7::app
