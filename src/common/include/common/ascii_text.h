#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace z7::common {

inline bool ascii_text_is_space(unsigned char ch) {
  return std::isspace(ch) != 0;
}

inline char ascii_text_to_lower(unsigned char ch) {
  return static_cast<char>(std::tolower(ch));
}

inline std::string trim_ascii_space_copy(std::string_view value) {
  size_t start = 0;
  size_t end = value.size();
  while (start < end &&
         ascii_text_is_space(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  while (end > start &&
         ascii_text_is_space(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(start, end - start));
}

inline std::string to_lower_ascii_copy(std::string_view value) {
  std::string lowered(value);
  for (char& ch : lowered) {
    ch = ascii_text_to_lower(static_cast<unsigned char>(ch));
  }
  return lowered;
}

}  // namespace z7::common
