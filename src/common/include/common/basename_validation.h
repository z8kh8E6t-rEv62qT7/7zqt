#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "common/ascii_text.h"
#include "common/path_text_normalization.h"

namespace z7::common {

enum class BasenameValidationError {
  kEmpty,
  kAbsolutePath,
  kDotOrDotDot,
  kContainsPathSeparator,
};

struct BasenameValidationResult {
  bool ok = false;
  std::string normalized_name;
  BasenameValidationError error = BasenameValidationError::kEmpty;
};

inline bool basename_validation_is_drive_absolute_path(std::string_view value) {
  return value.size() >= 3 &&
         std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
         value[1] == ':' &&
         value[2] == '/';
}

inline BasenameValidationResult validate_basename_only_name(
    std::string_view raw_name) {
  BasenameValidationResult result;
  std::string normalized =
      normalize_native_separators_copy(trim_ascii_space_copy(raw_name));
  if (normalized.empty()) {
    result.error = BasenameValidationError::kEmpty;
    return result;
  }
  if (normalized.front() == '/' ||
      basename_validation_is_drive_absolute_path(normalized)) {
    result.error = BasenameValidationError::kAbsolutePath;
    return result;
  }
  if (normalized == "." || normalized == "..") {
    result.error = BasenameValidationError::kDotOrDotDot;
    return result;
  }
  if (normalized.find('/') != std::string::npos) {
    result.error = BasenameValidationError::kContainsPathSeparator;
    return result;
  }

  result.ok = true;
  result.normalized_name = std::move(normalized);
  return result;
}

}  // namespace z7::common
