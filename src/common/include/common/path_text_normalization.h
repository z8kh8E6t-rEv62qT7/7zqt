#pragma once

#include <string>

namespace z7::common {

inline std::string normalize_native_separators_copy(std::string value) {
  for (char& ch : value) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return value;
}

}  // namespace z7::common
