// src/ui/gui/src/gui_task_runner_helpers_request.cpp
// Role: Build typed ArchiveRequest from GUI command line model.

#include "gui_task_runner_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "common/archive_type_normalization.h"
#include "extract_memory_settings.h"
#include "platform_support.h"

namespace z7::ui::gui {
namespace {

std::string to_lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool has_prefix_ci(const std::string& value, const std::string& prefix_lower) {
  if (value.size() < prefix_lower.size()) {
    return false;
  }
  return to_lower_ascii(value.substr(0, prefix_lower.size())) == prefix_lower;
}

bool is_unsigned_decimal(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isdigit(c) != 0;
  });
}

std::string deduce_archive_format(const AddTaskSpec& spec) {
  if (!spec.archive_type.empty()) {
    return z7::common::normalize_archive_type_token_copy(spec.archive_type);
  }
  if (spec.archive_path.empty()) {
    return "7z";
  }

  const std::filesystem::path archive_path(spec.archive_path);
  std::string ext = archive_path.extension().string();
  if (!ext.empty() && ext.front() == '.') {
    ext.erase(ext.begin());
  }
  ext = z7::common::canonical_archive_type_from_filename_suffix_copy(ext);
  if (ext.empty()) {
    return "7z";
  }
  return ext;
}

std::string side_by_side_sfx_module_path() {
  const QString path =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("7z.sfx"));
  const QByteArray encoded = QFile::encodeName(path);
  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

z7::app::OverwriteMode parse_overwrite_mode(const std::string& overwrite_switch) {
  const std::string lower = to_lower_ascii(overwrite_switch);
  if (has_prefix_ci(lower, "-aoa")) {
    return z7::app::OverwriteMode::kOverwrite;
  }
  if (has_prefix_ci(lower, "-aos")) {
    return z7::app::OverwriteMode::kSkip;
  }
  if (has_prefix_ci(lower, "-aou")) {
    return z7::app::OverwriteMode::kRenameExtracted;
  }
  if (has_prefix_ci(lower, "-aot")) {
    return z7::app::OverwriteMode::kRenameExisting;
  }
  return z7::app::OverwriteMode::kAsk;
}

z7::app::ExtractPathMode parse_extract_path_mode(const std::string& path_mode) {
  const std::string lower = to_lower_ascii(path_mode);
  if (lower == "no") {
    return z7::app::ExtractPathMode::kNoPaths;
  }
  if (lower == "absolute") {
    return z7::app::ExtractPathMode::kAbsolutePaths;
  }
  return z7::app::ExtractPathMode::kFullPaths;
}

z7::app::ExtractZoneIdMode parse_extract_zone_id_mode(
    const std::string& zone_id_mode) {
  std::string lower = to_lower_ascii(zone_id_mode);
  std::replace(lower.begin(), lower.end(), '-', '_');
  std::replace(lower.begin(), lower.end(), ' ', '_');
  if (lower == "all" || lower == "yes" || lower == "1") {
    return z7::app::ExtractZoneIdMode::kAll;
  }
  if (lower == "office" || lower == "2") {
    return z7::app::ExtractZoneIdMode::kOffice;
  }
  return z7::app::ExtractZoneIdMode::kNone;
}

z7::app::ExtractPathRemapMatchKind to_archive_extract_remap_match_kind(
    ExtractPathRemapMatchKind kind) {
  switch (kind) {
    case ExtractPathRemapMatchKind::kExactArchivePath:
      return z7::app::ExtractPathRemapMatchKind::kExactArchivePath;
    case ExtractPathRemapMatchKind::kArchivePrefix:
      return z7::app::ExtractPathRemapMatchKind::kArchivePrefix;
    case ExtractPathRemapMatchKind::kRequestRoot:
    default:
      return z7::app::ExtractPathRemapMatchKind::kRequestRoot;
  }
}

uint32_t benchmark_iterations_or_default(const GuiTaskSpec& spec) {
  const auto* benchmark =
      std::get_if<BenchmarkTaskSpec>(&spec);
  if (benchmark == nullptr) {
    return 10;
  }
  for (const std::string& operand : benchmark->operands) {
    if (!is_unsigned_decimal(operand)) {
      continue;
    }
    const unsigned long long parsed = std::strtoull(operand.c_str(), nullptr, 10);
    if (parsed == 0 ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
      continue;
    }
    return static_cast<uint32_t>(parsed);
  }
  return 10;
}

void apply_configured_extract_memory_limit(z7::app::ExtractRequest* request) {
  if (request == nullptr) {
    return;
  }
  const uint64_t bytes =
      z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  if (bytes == 0) {
    return;
  }
  request->configured_memory_limit_bytes = bytes;
  request->configured_memory_limit_defined = true;
}

void apply_configured_extract_memory_limit(z7::app::TestRequest* request) {
  if (request == nullptr) {
    return;
  }
  const uint64_t bytes =
      z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  if (bytes == 0) {
    return;
  }
  request->configured_memory_limit_bytes = bytes;
  request->configured_memory_limit_defined = true;
}

std::string join_extract_output_dir(const ExtractTaskSpec& spec) {
  const std::string base_output_dir = spec.output_dir.empty()
                                          ? std::filesystem::path(
                                                QDir::currentPath().toStdString())
                                                .string()
                                          : spec.output_dir;
  if (!spec.split_dest_enabled) {
    return base_output_dir;
  }

  const std::string split_name = spec.split_dest_name;
  if (split_name.empty()) {
    return base_output_dir;
  }

  return (std::filesystem::path(base_output_dir) /
          std::filesystem::path(split_name))
      .string();
}

}  // namespace

bool build_archive_request(const GuiTaskSpec& spec,
                           GuiTaskRunResult* out,
                           z7::app::ArchiveRequest* request_out) {
  if (out == nullptr || request_out == nullptr) {
    return false;
  }

  return std::visit(
      [&](const auto& typed_spec) -> bool {
        using T = std::decay_t<decltype(typed_spec)>;
        if constexpr (std::is_same_v<T, AddTaskSpec>) {
          if (typed_spec.archive_path.empty() || typed_spec.input_paths.empty()) {
            out->result = z7::app::make_immediate_result(
                7,
                z7::app::ArchiveErrorDomain::kInvalidArguments,
                "Add requires archive path and input files");
            return false;
          }

          z7::app::AddRequest request;
          request.archive_path = typed_spec.archive_path;
          request.format = deduce_archive_format(typed_spec);
          request.update_mode = typed_spec.update_mode;
          request.raw_update_switch = typed_spec.raw_update_switch;
          request.raw_update_switches = typed_spec.raw_update_switches;
          request.path_mode = typed_spec.path_mode;
          request.create_sfx = typed_spec.create_sfx;
          if (request.create_sfx) {
            request.sfx_module_path = side_by_side_sfx_module_path();
          }
          request.share_for_write = typed_spec.share_for_write;
          request.delete_after_compressing = typed_spec.delete_after_compressing;
          request.compression_level = typed_spec.compression_level;
          request.method_value = typed_spec.method_value;
          request.dictionary_size = typed_spec.dictionary_size;
          request.word_size = typed_spec.word_size;
          request.solid_block_size = typed_spec.solid_block_size;
          request.thread_count = typed_spec.thread_count;
          request.volume_size = typed_spec.volume_size;
          request.password = typed_spec.password;
          request.encrypt_headers_defined = typed_spec.encrypt_headers_defined;
          request.encrypt_headers = typed_spec.encrypt_headers;
          request.encryption_method = typed_spec.encryption_method;
          request.extra_parameters = typed_spec.extra_parameters;
          request.input_paths = typed_spec.input_paths;
          request_out->payload = std::move(request);
          return true;
        } else if constexpr (std::is_same_v<T, ExtractTaskSpec>) {
          std::vector<std::string> archive_inputs = typed_spec.archive_inputs;
          if (archive_inputs.empty()) {
            out->result = z7::app::make_immediate_result(
                7,
                z7::app::ArchiveErrorDomain::kInvalidArguments,
                "Extract requires archive path");
            return false;
          }

          z7::app::ExtractRequest request;
          request.output_dir = join_extract_output_dir(typed_spec);
          request.archive_type_hint = typed_spec.archive_type;
          request.overwrite_mode = parse_overwrite_mode(typed_spec.overwrite_switch);
          request.path_mode = parse_extract_path_mode(typed_spec.path_mode);
          request.eliminate_root_duplication = typed_spec.eliminate_root_duplication;
          request.zone_id_mode =
              parse_extract_zone_id_mode(typed_spec.zone_id_mode);
          request.path_remaps.reserve(typed_spec.path_remaps.size());
          for (const ExtractPathRemap& remap : typed_spec.path_remaps) {
            z7::app::ExtractPathRemap request_remap;
            request_remap.match_kind =
                to_archive_extract_remap_match_kind(remap.match_kind);
            request_remap.source_path = remap.source_path;
            request_remap.destination_path = remap.destination_path;
            request.path_remaps.push_back(std::move(request_remap));
          }
          request.password = typed_spec.password;
          request.restore_file_security =
              typed_spec.restore_file_security &&
              z7::ui::runtime_support::is_platform_supported(
                  z7::ui::runtime_support::PlatformSupport::kWindowsOnly);

          if (archive_inputs.size() > 1) {
            bool all_inputs_exist = true;
            for (const std::string& operand : archive_inputs) {
              std::error_code ec;
              if (!std::filesystem::exists(std::filesystem::path(operand), ec)) {
                all_inputs_exist = false;
                break;
              }
            }
            if (all_inputs_exist) {
              request.archive_paths = archive_inputs;
            } else {
              request.archive_path = archive_inputs.front();
              request.entries.assign(archive_inputs.begin() + 1, archive_inputs.end());
            }
          } else {
            request.archive_path = archive_inputs.front();
          }
          apply_configured_extract_memory_limit(&request);
          request_out->payload = std::move(request);
          return true;
        } else if constexpr (std::is_same_v<T, TestTaskSpec>) {
          if (typed_spec.archive_inputs.empty()) {
            out->result = z7::app::make_immediate_result(
                7,
                z7::app::ArchiveErrorDomain::kInvalidArguments,
                "Test requires archive path");
            return false;
          }
          z7::app::TestRequest request;
          request.archive_paths = typed_spec.archive_inputs;
          apply_configured_extract_memory_limit(&request);
          request_out->payload = std::move(request);
          return true;
        } else if constexpr (std::is_same_v<T, HashTaskSpec>) {
          if (typed_spec.input_paths.empty()) {
            out->result = z7::app::make_immediate_result(
                7,
                z7::app::ArchiveErrorDomain::kInvalidArguments,
                "Hash requires input files");
            return false;
          }
          z7::app::HashRequest request;
          request.input_paths = typed_spec.input_paths;
          request.hash_method =
              typed_spec.hash_method.empty() ? "CRC32" : typed_spec.hash_method;
          request.recursive_dirs = true;
          request_out->payload = std::move(request);
          return true;
        } else if constexpr (std::is_same_v<T, BenchmarkTaskSpec>) {
          z7::app::BenchmarkRequest request;
          request.iterations = benchmark_iterations_or_default(spec);
          request.thread_count = typed_spec.thread_count;
          request.dictionary_size = typed_spec.dictionary_size;
          request.total_mode = typed_spec.method_value == "*";
          request_out->payload = std::move(request);
          return true;
        } else if constexpr (std::is_same_v<T, ArchiveExportTaskSpec> ||
                             std::is_same_v<T, ArchiveHashTaskSpec> ||
                             std::is_same_v<T, ArchiveTestTaskSpec>) {
          out->result = z7::app::make_immediate_result(
              2,
              z7::app::ArchiveErrorDomain::kUnsupportedFormat,
              "Command must use specialized spec execution path");
          return false;
        } else {
          out->result = z7::app::make_immediate_result(
              2,
              z7::app::ArchiveErrorDomain::kUnsupportedFormat,
              "Unsupported command in spec-only direct-request path");
          return false;
        }
      },
      spec);
}

}  // namespace z7::ui::gui
