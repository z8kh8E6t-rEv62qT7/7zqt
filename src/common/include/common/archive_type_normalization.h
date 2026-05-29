#pragma once

#include <string>
#include <string_view>

#include "common/ascii_text.h"

namespace z7::common {

inline std::string normalize_archive_type_token_copy(std::string_view raw_token) {
  return to_lower_ascii_copy(trim_ascii_space_copy(raw_token));
}

inline bool is_legacy_archive_type_alias(std::string_view raw_token) {
  const std::string token = normalize_archive_type_token_copy(raw_token);
  return token == "gz" || token == "bz2";
}

inline std::string canonical_archive_type_token_copy(std::string_view raw_token) {
  const std::string token = normalize_archive_type_token_copy(raw_token);
  if (token == "gz" || token == "gzip") {
    return "gzip";
  }
  if (token == "bz2" || token == "bzip2") {
    return "bzip2";
  }
  return token;
}

inline bool is_gzip_family_archive_suffix(std::string_view raw_suffix) {
  const std::string suffix = normalize_archive_type_token_copy(raw_suffix);
  return suffix == "gz" || suffix == "gzip" || suffix == "tgz" ||
         suffix == "tpz" || suffix == "tar.gz";
}

inline bool is_bzip2_family_archive_suffix(std::string_view raw_suffix) {
  const std::string suffix = normalize_archive_type_token_copy(raw_suffix);
  return suffix == "bz2" || suffix == "bzip2" || suffix == "tbz2" ||
         suffix == "tbz" || suffix == "tar.bz2";
}

inline std::string canonical_archive_type_from_filename_suffix_copy(
    std::string_view raw_suffix) {
  if (is_gzip_family_archive_suffix(raw_suffix)) {
    return "gzip";
  }
  if (is_bzip2_family_archive_suffix(raw_suffix)) {
    return "bzip2";
  }
  return canonical_archive_type_token_copy(raw_suffix);
}

inline std::string preferred_archive_output_suffix_copy(
    std::string_view archive_type_token) {
  const std::string canonical =
      canonical_archive_type_token_copy(archive_type_token);
  if (canonical == "gzip") {
    return "gz";
  }
  if (canonical == "bzip2") {
    return "bz2";
  }
  return canonical;
}

}  // namespace z7::common
