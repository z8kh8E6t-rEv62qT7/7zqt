#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "archive_types_base.h"

namespace z7::app {

struct HashMethodDigest {
  std::string method_name;
  std::string data_sum;
  std::string names_sum;
  std::string streams_sum;
  bool has_data_sum = false;
  bool has_names_sum = false;
  bool has_streams_sum = false;
};

struct HashSummary {
  uint64_t num_archives = 0;
  uint64_t num_dirs = 0;
  uint64_t num_files = 0;
  uint64_t num_alt_streams = 0;
  uint64_t files_size = 0;
  uint64_t alt_streams_size = 0;
  bool physical_size_defined = false;
  uint64_t physical_size = 0;
  uint64_t num_errors = 0;
  std::string main_name;
  std::string first_file_name;
  std::vector<HashMethodDigest> methods;
};

struct HashRequest {
  // Filesystem mode inputs.
  std::vector<std::string> input_paths;
  // Archive mode entry selection relative to the addressed session root.
  std::vector<std::string> entries;
  std::optional<ArchiveSessionToken> session_token;
  std::string hash_method;
  bool recursive_dirs = true;
};

}  // namespace z7::app
