#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace z7::ui::gui {

enum class ExtractPathRemapMatchKind {
  kRequestRoot,
  kExactArchivePath,
  kArchivePrefix
};

struct ExtractPathRemap {
  ExtractPathRemapMatchKind match_kind = ExtractPathRemapMatchKind::kRequestRoot;
  std::string source_path;
  std::string destination_path;
};

struct AddTaskSpec {
  bool show_dialog = false;
  std::string archive_path;
  std::string archive_type;
  std::string update_mode = "add";
  std::string raw_update_switch;
  std::vector<std::string> raw_update_switches;
  std::string path_mode = "relative";
  bool create_sfx = false;
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
  std::vector<std::string> extra_parameters;
  std::vector<std::string> input_paths;
};

struct ExtractTaskSpec {
  bool show_dialog = false;
  std::string output_dir;
  bool split_dest_enabled = false;
  std::string split_dest_name;
  std::string overwrite_switch;
  std::string path_mode = "full";
  bool eliminate_root_duplication = false;
  std::vector<ExtractPathRemap> path_remaps;
  bool restore_file_security = false;
  std::string zone_id_mode = "none";
  std::string archive_type;
  std::string password;
  std::vector<std::string> archive_inputs;
};

struct ArchiveExportTaskSpec {
  std::string root_archive_path;
  std::string root_archive_type;
  std::vector<std::string> nested_archive_entries;
  std::vector<std::string> archive_entry_paths;
  std::string output_dir;
  std::string overwrite_mode = "ask";
  std::string path_mode = "full";
  bool eliminate_root_duplication = false;
  std::vector<ExtractPathRemap> path_remaps;
  bool restore_file_security = false;
  std::string zone_id_mode = "none";
  std::string password;
};

struct TestTaskSpec {
  std::vector<std::string> archive_inputs;
};

struct HashTaskSpec {
  std::string hash_method;
  std::vector<std::string> input_paths;
};

struct ArchiveHashTaskSpec {
  std::string archive_path;
  std::string archive_type;
  std::vector<std::string> nested_archive_entries;
  std::string hash_method;
  std::vector<std::string> archive_entry_paths;
};

struct ArchiveTestTaskSpec {
  std::string archive_path;
  std::string archive_type;
  std::vector<std::string> nested_archive_entries;
  std::vector<std::string> archive_entry_paths;
};

struct BenchmarkTaskSpec {
  std::string method_value;
  std::string dictionary_size;
  std::string thread_count;
  std::vector<std::string> operands;
};

struct OpenTaskSpec {
  std::string archive_path;
  std::string archive_type;
  std::vector<std::string> nested_archive_entries;
  std::string entry_path;
};

using GuiTaskSpec = std::variant<AddTaskSpec,
                                 ExtractTaskSpec,
                                 ArchiveExportTaskSpec,
                                 TestTaskSpec,
                                 HashTaskSpec,
                                 ArchiveHashTaskSpec,
                                 ArchiveTestTaskSpec,
                                 BenchmarkTaskSpec,
                                 OpenTaskSpec>;

}  // namespace z7::ui::gui
