#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "archive_error.h"

namespace z7::app {

enum class OverwriteMode {
  kAsk,
  kOverwrite,
  kSkip,
  kRenameExisting,
  kRenameExtracted
};

enum class CreateNodeKind {
  kFile,
  kDirectory
};

enum class OperationEventKind {
  kLifecycle,
  kLog,
  kProgress
};

enum class OperationStage {
  kPrepare,
  kRunning,
  kFinalizing,
  kCompleted
};

enum class OutputChannel {
  kNone,
  kStdOut,
  kStdErr
};

struct BackendCapabilities {
  bool supports_split = false;
  bool supports_combine = false;
  bool supports_typed_benchmark = false;
};

struct NativeExecutionInfo {
  int native_exit_code = 0;
  NativeTerminationReason termination_reason =
      NativeTerminationReason::kCompleted;
};

struct AddInputItem {
  // Source path on the local filesystem. May name a file or directory.
  std::string filesystem_path;
  // Full UTF-8 archive-relative destination path. Must not be empty.
  std::string archive_entry;
};

struct ArchiveSessionToken {
  uint64_t value = 0;

  bool is_valid() const {
    return value != 0;
  }

  explicit operator bool() const {
    return is_valid();
  }

  friend bool operator==(const ArchiveSessionToken& lhs,
                         const ArchiveSessionToken& rhs) = default;
};

struct AddRequest {
  std::string archive_path;
  std::string format;
  // Optional session target for archive-view updates. When set, the writable
  // archive is the already-open session rather than archive_path directly.
  std::optional<ArchiveSessionToken> session_token;
  // add/update/fresh/sync (mapped from command token and -u switch).
  std::string update_mode = "add";
  // Raw -u switch payload when parser cannot map to a known update_mode.
  std::string raw_update_switch;
  // Ordered raw -u switch payload list for original multi-switch semantics.
  std::vector<std::string> raw_update_switches;
  // UTF-8 archive virtual directory. Empty means archive root.
  std::string directory;
  // relative/full/absolute (mapped from -spf*).
  std::string path_mode = "relative";
  bool create_sfx = false;
  // Filesystem path to the SFX module used when create_sfx is true.
  std::string sfx_module_path;
  bool share_for_write = false;
  bool delete_after_compressing = false;
  std::string compression_level;
  std::string method_value;
  std::string dictionary_size;
  std::string word_size;
  std::string solid_block_size;
  std::string thread_count;
  std::string volume_size;
  std::string password;
  bool encrypt_headers_defined = false;
  bool encrypt_headers = false;
  std::string encryption_method;
  // Raw -m* payload list from CLI/dialog advanced parameters.
  std::vector<std::string> extra_parameters;
  // Explicit filesystem-path -> archive-entry mapping. When non-empty this is
  // the only accepted input mode: `input_paths` and `directory` must be empty.
  // Each archive_entry is a complete archive-relative destination path.
  std::vector<AddInputItem> input_items;
  std::vector<std::string> input_paths;
};

struct CompressionResourcesEstimate {
  bool ok = false;
  uint64_t compression_usage_bytes = 0;
  uint64_t decompression_usage_bytes = 0;
  uint64_t resolved_dictionary_bytes = 0;
  uint32_t resolved_threads = 1;
  uint64_t configured_memory_limit_bytes = 0;
  bool configured_memory_limit_defined = false;
  std::string summary;
};

struct TestRequest {
  std::string archive_path;
  // Optional multi-archive test source list.
  std::vector<std::string> archive_paths;
  // Optional archive entry selection for archive-view test behavior.
  std::vector<std::string> entries;
  std::optional<ArchiveSessionToken> session_token;
  uint64_t configured_memory_limit_bytes = 0;
  bool configured_memory_limit_defined = false;
};

struct SplitRequest {
  std::string source_file_path;
  std::string output_dir;
  std::string volume_size_spec;
};

struct CombineRequest {
  std::string source_part_path;
  std::string output_dir;
};

struct DeleteRequest {
  std::string archive_path;
  // Optional session target for archive-view deletes. When set, delete runs
  // against the already-open session rather than archive_path directly.
  std::optional<ArchiveSessionToken> session_token;
  std::vector<std::string> entries;
  std::string password;
  // Optional filesystem deletion source list.
  std::vector<std::string> filesystem_paths;
  bool use_recycle_bin = true;
};

struct OpenArchiveRequest {
  std::string archive_path;
  std::string archive_type_hint;
};

struct OpenArchiveFromPathRequest {
  std::string archive_path;
  std::string archive_type_hint;
};

struct OpenArchiveFromParentRequest {
  ArchiveSessionToken parent;
  // Explicit entry selector for nested opens. Callers must set exactly one of
  // `entry_index` or `entry_path`; the backend rejects missing or ambiguous
  // selectors instead of defaulting to the first item in the parent archive.
  std::optional<uint32_t> entry_index;
  // Optional archive-relative entry path. When set, callers must leave
  // `entry_index` unset.
  std::string entry_path;
  std::string archive_type_hint;
  size_t size_budget = 0;
  std::string display_path_hint;
};

struct CloseArchiveSessionRequest {
  ArchiveSessionToken token;
};

struct ListRequest {
  std::string archive_path;
  // UTF-8 path inside archive. Empty means archive root.
  std::string directory;
  // Optional hint such as "*" / "#" / "7z" (same syntax as original arcFormat).
  std::string archive_type_hint;
  std::optional<ArchiveSessionToken> session_token;
  // When true, list the full subtree below `directory` using relative entry
  // paths instead of only the immediate children.
  bool recursive_dirs = false;
  // When true, include detailed item properties for 7zFM-like archive preview
  // columns. Keep false for lightweight listing.
  bool include_detailed_props = false;
  // When true, backend delivers entries via IArchiveDelegate::on_list_entries_batch
  // instead of accumulating them in ListResult::entries (which will be empty).
  // Callers must override on_list_entries_batch to collect results. The two
  // modes are mutually exclusive to avoid doubling memory usage.
  bool streaming_mode = false;
  // Hint for the number of entries per batch when streaming_mode is true.
  // Backend may round up or down; defaults to 256 when absent.
  std::optional<size_t> batch_size_hint;
};

struct ArchiveListEntry {
  std::string path;
  bool is_dir = false;
  uint64_t size = 0;
  std::optional<uint64_t> packed_size;
  std::optional<int64_t> mtime_msecs_utc;
  std::optional<int64_t> ctime_msecs_utc;
  std::optional<int64_t> atime_msecs_utc;
  std::string attributes;
  std::optional<bool> encrypted;
  std::string comment;
  std::optional<uint32_t> crc;
  std::string method;
  std::string characts;
  std::string host_os;
  std::string version;
  std::optional<uint64_t> volume_index;
  std::optional<uint64_t> offset;
  std::optional<uint64_t> num_sub_dirs;
  std::optional<uint64_t> num_sub_files;
};

struct NavigateRequest {
  std::string from_path;
  std::string to_path;
};

struct CopyRequest {
  std::vector<std::string> source_paths;
  std::string destination_dir;
  // Optional explicit destination path for single-source copy.
  std::string destination_path;
  OverwriteMode overwrite_mode = OverwriteMode::kOverwrite;
};

struct MoveRequest {
  std::vector<std::string> source_paths;
  std::string destination_dir;
  // Optional explicit destination path for single-source move.
  std::string destination_path;
  OverwriteMode overwrite_mode = OverwriteMode::kOverwrite;
};

struct RenameRequest {
  std::string archive_path;
  std::optional<ArchiveSessionToken> session_token;
  std::string entry_path;
  std::string source_path;
  std::string new_name;
  bool entry_is_dir = false;
};

struct CreateRequest {
  std::string parent_dir;
  std::string name;
  CreateNodeKind kind = CreateNodeKind::kFile;
};

struct ArchiveCommentRequest {
  std::string archive_path;
  std::string entry_path;
  std::optional<ArchiveSessionToken> session_token;
  std::string comment;
};

struct FilesystemCommentRequest {
  std::string directory_path;
  std::string entry_name;
  std::string comment;
};

// Query metadata for a single archive entry without extracting it.
// Never prompts for a password; fails with kPassword if the archive requires
// one and no open session with cached credentials is available.
struct GetEntryInfoRequest {
  std::string archive_path;
  std::string archive_type_hint;
  std::optional<ArchiveSessionToken> session_token;
  // Archive-relative path to query. Empty string or "/" means the archive root.
  std::string entry_path;
};

}  // namespace z7::app
