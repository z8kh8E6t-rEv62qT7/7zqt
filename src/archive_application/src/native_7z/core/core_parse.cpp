// src/archive_application/src/native_7z/core/core_parse.cpp
// Role: Fundamental parsing, conversion, and base result helpers.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

bool is_auto_value(const std::string& value) {
  const std::string normalized =
      z7::common::to_lower_ascii_copy(z7::common::trim_ascii_space_copy(value));
  return normalized.empty() || normalized == "auto";
}

bool parse_uint64_decimal(const std::string& value, uint64_t& out) {
  if (value.empty()) {
    return false;
  }
  uint64_t parsed = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<uint64_t>(ch - '0');
  }
  out = parsed;
  return true;
}

uint64_t parse_size_to_bytes_or_default(const std::string& raw,
                                        uint64_t default_bytes) {
  std::string value =
      z7::common::to_lower_ascii_copy(z7::common::trim_ascii_space_copy(raw));
  if (value.empty()) {
    return default_bytes;
  }

  uint64_t mult = 1;
  const char suffix = value.back();
  if (suffix == 'k' || suffix == 'm' || suffix == 'g' || suffix == 't') {
    value.pop_back();
    switch (suffix) {
      case 'k':
        mult = 1ULL << 10;
        break;
      case 'm':
        mult = 1ULL << 20;
        break;
      case 'g':
        mult = 1ULL << 30;
        break;
      case 't':
        mult = 1ULL << 40;
        break;
      default:
        break;
    }
  }

  uint64_t base = 0;
  if (!parse_uint64_decimal(value, base)) {
    return default_bytes;
  }
  if (base == 0) {
    return default_bytes;
  }
  return base * mult;
}

uint32_t parse_thread_count_or_default(const std::string& raw,
                                       uint32_t default_threads) {
  if (is_auto_value(raw)) {
    return default_threads;
  }

  uint64_t parsed = 0;
  if (!parse_uint64_decimal(z7::common::trim_ascii_space_copy(raw), parsed) ||
      parsed == 0) {
    return default_threads;
  }
  if (parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return default_threads;
  }
  return static_cast<uint32_t>(parsed);
}

bool parse_volume_sizes_spec(const std::string& input,
                             std::vector<uint64_t>& values) {
  values.clear();
  bool prev_is_number = false;

  for (size_t i = 0; i < input.size();) {
    const char c = input[i++];
    if (c == ' ') {
      continue;
    }
    if (c == '-') {
      // Keep original behavior: everything after '-' is treated as comment.
      return true;
    }

    if (prev_is_number) {
      prev_is_number = false;
      unsigned num_bits = 0;
      switch (static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) {
        case 'b':
          continue;
        case 'k':
          num_bits = 10;
          break;
        case 'm':
          num_bits = 20;
          break;
        case 'g':
          num_bits = 30;
          break;
        case 't':
          num_bits = 40;
          break;
        default:
          break;
      }
      if (num_bits != 0) {
        uint64_t& val = values.back();
        if (val >= (static_cast<uint64_t>(1) << (64 - num_bits))) {
          return false;
        }
        val <<= num_bits;
        while (i < input.size() && input[i] != ' ') {
          ++i;
        }
        continue;
      }
    }

    --i;
    const size_t start = i;
    uint64_t val = 0;
    while (i < input.size() &&
           std::isdigit(static_cast<unsigned char>(input[i])) != 0) {
      val = val * 10 + static_cast<uint64_t>(input[i] - '0');
      ++i;
    }
    if (i == start || val == 0) {
      return false;
    }
    values.push_back(val);
    prev_is_number = true;
  }

  return true;
}

uint64_t get_number_of_volumes(uint64_t size,
                               const std::vector<uint64_t>& vol_sizes) {
  if (size == 0 || vol_sizes.empty()) {
    return 1;
  }
  for (size_t i = 0; i < vol_sizes.size(); ++i) {
    const uint64_t vol_size = vol_sizes[i];
    if (vol_size >= size) {
      return static_cast<uint64_t>(i + 1);
    }
    size -= vol_size;
  }

  const uint64_t vol_size = vol_sizes.back();
  if (vol_size == 0) {
    return std::numeric_limits<uint64_t>::max();
  }
  return static_cast<uint64_t>(vol_sizes.size()) + (size - 1) / vol_size + 1;
}

size_t volume_number_digits(uint64_t num_volumes) {
  size_t digits = 3;
  while (num_volumes > 999) {
    num_volumes /= 10;
    ++digits;
  }
  return digits;
}

std::string volume_sequence_name(uint64_t index, size_t digits) {
  std::string out = std::to_string(index);
  if (out.size() < digits) {
    out.insert(out.begin(), digits - out.size(), '0');
  }
  return out;
}

std::string VolumeSequenceState::next_name() {
  for (int i = static_cast<int>(changed_part.size()) - 1; i >= 0; --i) {
    const char c = changed_part[static_cast<size_t>(i)];
    if (c != '9') {
      changed_part[static_cast<size_t>(i)] = static_cast<char>(c + 1);
      break;
    }
    changed_part[static_cast<size_t>(i)] = '0';
    if (i == 0) {
      changed_part.insert(changed_part.begin(), '1');
    }
  }
  return unchanged_part + changed_part;
}

bool VolumeSequenceState::parse_name(const std::string& name) {
  if (name.size() < 2 || name.back() != '1' || name[name.size() - 2] != '0') {
    return false;
  }

  size_t pos = name.size() - 2;
  while (pos > 0 && name[pos - 1] == '0') {
    --pos;
  }
  unchanged_part = name.substr(0, pos);
  changed_part = name.substr(pos);
  return true;
}

std::string astring_to_std(const AString& value) {
  return std::string(value.Ptr(), static_cast<size_t>(value.Len()));
}

std::string ustring_to_utf8(const UString& value) {
  AString utf8;
  ConvertUnicodeToUTF8(value, utf8);
  return astring_to_std(utf8);
}

UString utf8_to_ustring(const std::string& value) {
  UString out;
  if (value.empty()) {
    return out;
  }

  AString utf8(value.c_str());
  if (!ConvertUTF8ToUnicode(utf8, out)) {
    for (char ch : value) {
      out += static_cast<wchar_t>(static_cast<unsigned char>(ch));
    }
  }
  return out;
}

OperationResult invalid_request(const std::string& summary) {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = 7;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(ArchiveErrorDomain::kInvalidArguments, summary, 7);
  result.summary = summary;
  return result;
}

OperationResult unsupported_request(const std::string& summary) {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = 2;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(ArchiveErrorDomain::kUnsupportedFormat, summary, 2);
  result.summary = summary;
  return result;
}

}  // namespace z7::app
