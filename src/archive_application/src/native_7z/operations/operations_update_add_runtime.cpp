// src/archive_application/src/native_7z/operations/operations_update_add_runtime.cpp
// Role: Add-request runtime option parsing and compression resource estimation.

#include "core/internal.h"
#include "common/archive_type_normalization.h"
#include "third_party_adapter/third_party_adapter.h"

#include <filesystem>

namespace z7::app {
namespace {

void add_method_property(CObjectVector<CProperty>& properties,
                         const std::string& name,
                         const std::string& value = {}) {
  CProperty prop;
  prop.Name = utf8_to_ustring(name);
  prop.Value = utf8_to_ustring(value);
  properties.Add(prop);
}

bool is_valid_level_value(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  uint64_t parsed = 0;
  return parse_uint64_decimal(value, parsed) && parsed <= 9;
}

bool parse_add_path_mode(const std::string& path_mode_raw,
                         NWildcard::ECensorPathMode* out_mode) {
  if (out_mode == nullptr) {
    return false;
  }
  const std::string path_mode = z7::common::to_lower_ascii_copy(
      z7::common::trim_ascii_space_copy(path_mode_raw));
  if (path_mode.empty() || path_mode == "relative") {
    *out_mode = NWildcard::k_RelatPath;
    return true;
  }
  if (path_mode == "full") {
    *out_mode = NWildcard::k_FullPath;
    return true;
  }
  if (path_mode == "absolute") {
    *out_mode = NWildcard::k_AbsPath;
    return true;
  }
  return false;
}

bool parse_positive_thread_count(const std::string& raw, std::string* normalized) {
  if (normalized == nullptr) {
    return false;
  }
  const std::string value = z7::common::trim_ascii_space_copy(raw);
  if (value.empty() || is_auto_value(value)) {
    normalized->clear();
    return true;
  }
  uint64_t parsed = 0;
  if (!parse_uint64_decimal(value, parsed) || parsed == 0 ||
      parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return false;
  }
  *normalized = std::to_string(parsed);
  return true;
}

bool parse_size_like_method_value(const std::string& raw,
                                  bool allow_on_off,
                                  std::string* normalized) {
  if (normalized == nullptr) {
    return false;
  }
  const std::string value = z7::common::trim_ascii_space_copy(raw);
  if (value.empty()) {
    normalized->clear();
    return true;
  }
  const std::string lower = z7::common::to_lower_ascii_copy(value);
  if (allow_on_off && (lower == "on" || lower == "off")) {
    *normalized = lower;
    return true;
  }
  constexpr uint64_t kInvalidSentinel = std::numeric_limits<uint64_t>::max();
  if (parse_size_to_bytes_or_default(lower, kInvalidSentinel) == kInvalidSentinel) {
    return false;
  }
  *normalized = value;
  return true;
}

std::string normalize_extra_property_token(const std::string& raw) {
  std::string token = z7::common::trim_ascii_space_copy(raw);
  if (token.empty()) {
    return {};
  }

  if (token.front() != '-') {
    return {};
  }

  token.erase(token.begin());
  if (token.empty()) {
    return {};
  }

  if (!token.empty() && (token.front() == 'm' || token.front() == 'M')) {
    token.erase(token.begin());
  }
  return z7::common::trim_ascii_space_copy(token);
}

bool append_extra_method_properties(const std::vector<std::string>& raw_tokens,
                                    CObjectVector<CProperty>& properties,
                                    std::string* error_summary) {
  for (const std::string& raw_token : raw_tokens) {
    const std::string trimmed = z7::common::trim_ascii_space_copy(raw_token);
    const std::string token = normalize_extra_property_token(raw_token);
    if (token.empty()) {
      if (!trimmed.empty()) {
        if (error_summary != nullptr) {
          *error_summary = "Invalid compression property token: " + raw_token;
        }
        return false;
      }
      continue;
    }
    const size_t separator = token.find('=');
    const std::string name =
        separator == std::string::npos ? token : token.substr(0, separator);
    if (name.empty()) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid compression property token: " + raw_token;
      }
      return false;
    }
    const std::string value =
        separator == std::string::npos ? std::string() : token.substr(separator + 1);
    add_method_property(properties, name, value);
  }
  return true;
}

bool add_format_supports_encrypted_headers(const std::string& format) {
  return z7::common::canonical_archive_type_token_copy(format) == "7z";
}

}  // namespace

bool apply_add_runtime_options(const AddRequest& request,
                               CUpdateOptions& options,
                               std::string* error_summary) {
  if (!parse_add_path_mode(request.path_mode, &options.PathMode)) {
    if (error_summary != nullptr) {
      *error_summary = "Invalid path mode: " + request.path_mode;
    }
    return false;
  }
  options.OpenShareForWrite = request.share_for_write;
  options.DeleteAfterCompressing = request.delete_after_compressing;
  options.SfxMode = request.create_sfx;
  if (options.SfxMode) {
    const std::string format =
        z7::common::canonical_archive_type_token_copy(request.format);
    if (format != "7z") {
      if (error_summary != nullptr) {
        *error_summary = "SFX archives are supported only for 7z archives";
      }
      return false;
    }
    if (request.sfx_module_path.empty()) {
      if (error_summary != nullptr) {
        *error_summary = "SFX module path is not specified";
      }
      return false;
    }
    std::error_code ec;
    const std::filesystem::path module_path(request.sfx_module_path);
    if (!std::filesystem::is_regular_file(module_path, ec)) {
      if (error_summary != nullptr) {
        *error_summary = "SFX module file does not exist: " +
                         request.sfx_module_path;
      }
      return false;
    }
    options.SfxModule = us2fs(utf8_to_ustring(request.sfx_module_path));
  }

  if (!request.volume_size.empty()) {
    std::vector<uint64_t> parsed_volumes;
    if (!parse_volume_sizes_spec(request.volume_size, parsed_volumes) ||
        parsed_volumes.empty()) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid volume size: " + request.volume_size;
      }
      return false;
    }
    for (uint64_t size : parsed_volumes) {
      options.VolumesSizes.Add(size);
    }
  }
  if (options.SfxMode && options.VolumesSizes.Size() > 0) {
    if (error_summary != nullptr) {
      *error_summary = "SFX archives cannot be split into volumes";
    }
    return false;
  }

  const bool is_7z = add_format_supports_encrypted_headers(request.format);
  CObjectVector<CProperty>& properties = options.MethodMode.Properties;

  if (!request.compression_level.empty()) {
    const std::string level =
        z7::common::trim_ascii_space_copy(request.compression_level);
    if (!is_valid_level_value(level)) {
      if (error_summary != nullptr) {
        *error_summary = "Invalid compression level: " + request.compression_level;
      }
      return false;
    }
    add_method_property(properties, "x", level);
  }

  if (!request.method_value.empty()) {
    add_method_property(properties,
                        is_7z ? "0" : "m",
                        z7::common::trim_ascii_space_copy(request.method_value));
  }

  std::string dictionary_size;
  if (!parse_size_like_method_value(request.dictionary_size, false, &dictionary_size)) {
    if (error_summary != nullptr) {
      *error_summary = "Invalid dictionary size: " + request.dictionary_size;
    }
    return false;
  }
  if (!dictionary_size.empty()) {
    add_method_property(properties, is_7z ? "0d" : "d", dictionary_size);
  }

  std::string word_size;
  if (!parse_size_like_method_value(request.word_size, false, &word_size)) {
    if (error_summary != nullptr) {
      *error_summary = "Invalid word size: " + request.word_size;
    }
    return false;
  }
  if (!word_size.empty()) {
    add_method_property(properties, is_7z ? "0fb" : "fb", word_size);
  }

  std::string solid_block_size;
  if (!parse_size_like_method_value(request.solid_block_size, true, &solid_block_size)) {
    if (error_summary != nullptr) {
      *error_summary = "Invalid solid block size: " + request.solid_block_size;
    }
    return false;
  }
  if (!solid_block_size.empty()) {
    add_method_property(properties, "s", solid_block_size);
  }

  std::string thread_count;
  if (!parse_positive_thread_count(request.thread_count, &thread_count)) {
    if (error_summary != nullptr) {
      *error_summary = "Invalid thread count: " + request.thread_count;
    }
    return false;
  }
  if (!thread_count.empty()) {
    add_method_property(properties, "mt", thread_count);
  }

  if (!request.password.empty()) {
    if (!request.encryption_method.empty()) {
      add_method_property(properties, "em", request.encryption_method);
    }
    if (request.encrypt_headers_defined) {
      if (is_7z) {
        add_method_property(properties, "he", request.encrypt_headers ? "on" : "off");
      } else if (request.encrypt_headers) {
        if (error_summary != nullptr) {
          *error_summary =
              "Encrypted file names are supported only for 7z archives";
        }
        return false;
      }
    }
  }

  if (!append_extra_method_properties(request.extra_parameters, properties, error_summary)) {
    return false;
  }

  return true;
}

CompressionResourcesEstimate estimate_compression_resources_for_request(
    const AddRequest& request) {
  CompressionResourcesEstimate out;
  CUpdateOptions options;
  std::string options_error;
  if (!apply_add_runtime_options(request, options, &options_error)) {
    out.ok = false;
    out.summary = options_error.empty() ? "Invalid compression options" : options_error;
    return out;
  }

  uint32_t cpu_threads = 1;
#ifndef Z7_ST
  cpu_threads = NWindows::NSystem::GetNumberOfProcessors();
#endif
  if (cpu_threads == 0) {
    cpu_threads = 1;
  }

  out.resolved_threads =
      std::max<uint32_t>(1, parse_thread_count_or_default(request.thread_count, cpu_threads));
  out.resolved_dictionary_bytes =
      parse_size_to_bytes_or_default(request.dictionary_size, 32ULL << 20);

  const std::string method = z7::common::to_lower_ascii_copy(
      z7::common::trim_ascii_space_copy(request.method_value));
  uint64_t compress_multiplier = 2;
  uint64_t decompress_divider = 2;
  if (method == "copy" || method == "store") {
    compress_multiplier = 1;
    decompress_divider = 1;
  } else if (method == "bzip2") {
    compress_multiplier = 4;
    decompress_divider = 1;
  } else if (method == "ppmd" || method == "ppmdzip") {
    compress_multiplier = 3;
    decompress_divider = 1;
  }

  out.compression_usage_bytes = out.resolved_dictionary_bytes *
                                static_cast<uint64_t>(out.resolved_threads) *
                                compress_multiplier;
  out.decompression_usage_bytes = out.resolved_dictionary_bytes /
                                  std::max<uint64_t>(1, decompress_divider);

  FOR_VECTOR (i, options.MethodMode.Properties) {
    const CProperty& prop = options.MethodMode.Properties[i];
    const std::string name = z7::common::to_lower_ascii_copy(
        z7::common::trim_ascii_space_copy(ustring_to_utf8(prop.Name)));
    if (name != "memuse") {
      continue;
    }
    const std::string value = z7::common::to_lower_ascii_copy(
        z7::common::trim_ascii_space_copy(ustring_to_utf8(prop.Value)));
    if (value.empty()) {
      continue;
    }
    constexpr uint64_t kInvalidSentinel = std::numeric_limits<uint64_t>::max();
    const uint64_t parsed = parse_size_to_bytes_or_default(value, kInvalidSentinel);
    if (parsed != kInvalidSentinel && parsed != 0) {
      out.configured_memory_limit_bytes = parsed;
      out.configured_memory_limit_defined = true;
    }
    break;
  }

  out.ok = true;
  out.summary = "Estimated from parsed compression settings";
  return out;
}

}  // namespace z7::app
