#include "benchmark_typed_parser.h"

#include <regex>
#include <string>

#include "common/ascii_text.h"

namespace z7::app {
namespace {

bool parse_uint64(const std::string& text, uint64_t* out) {
  if (out == nullptr || text.empty()) {
    return false;
  }
  uint64_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint64_t>(ch - '0');
  }
  *out = value;
  return true;
}

bool parse_metrics_match(const std::smatch& match,
                         bool has_dictionary,
                         BenchmarkTypedMetrics* out) {
  if (out == nullptr) {
    return false;
  }

  constexpr int kWithoutDictCount = 8;
  constexpr int kWithDictCount = 9;
  const int expected_count = has_dictionary ? kWithDictCount : kWithoutDictCount;
  if (static_cast<int>(match.size()) != expected_count + 1) {
    return false;
  }

  BenchmarkTypedMetrics metrics;
  int offset = 1;
  if (has_dictionary) {
    uint64_t dict = 0;
    if (!parse_uint64(match[1].str(), &dict)) {
      return false;
    }
    metrics.has_dictionary_log = true;
    metrics.dictionary_log = static_cast<uint32_t>(dict);
    offset = 2;
  }

  uint64_t values[8] = {};
  for (int i = 0; i < 8; ++i) {
    if (!parse_uint64(match[offset + i].str(), &values[i])) {
      return false;
    }
  }

  metrics.compress_speed_kb_per_sec = values[0];
  metrics.compress_cpu_usage_percent = values[1];
  metrics.compress_rpu_mips = values[2];
  metrics.compress_rating_mips = values[3];
  metrics.decompress_speed_kb_per_sec = values[4];
  metrics.decompress_cpu_usage_percent = values[5];
  metrics.decompress_rpu_mips = values[6];
  metrics.decompress_rating_mips = values[7];
  *out = metrics;
  return true;
}

}  // namespace

std::optional<BenchmarkTypedSnapshot> BenchmarkTypedParser::consume_line(
    const std::string& line) {
  static const std::regex kDictLine(
      R"(^(\d+):\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\|\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$)");
  static const std::regex kAvrLine(
      R"(^Avr:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\|\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$)");
  static const std::regex kTotLine(
      R"(^Tot:\s+(\d+)\s+(\d+)\s+(\d+)(?:\s+\d+\s+\d+)?\s*$)");
  static const std::regex kRamSizeLine(
      R"(^RAM size:\s+([0-9]+)\s*MB.*$)",
      std::regex_constants::icase);
  static const std::regex kRamUsageLine(
      R"(^RAM usage:\s+([0-9]+)\s*MB.*$)",
      std::regex_constants::icase);
  static const std::regex kFrequencyLine(
      R"(^\d+T\s+(?:CPU\s+)?(?:Freq|Frequency)\s+\(MHz\):.*$)");

  const std::string trimmed = z7::common::trim_ascii_space_copy(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  std::smatch match;
  BenchmarkTypedSnapshot snapshot;

  if (std::regex_match(trimmed, match, kDictLine)) {
    snapshot.kind = BenchmarkSnapshotKind::kDictionaryPass;
    if (!parse_metrics_match(match, true, &snapshot.metrics)) {
      return std::nullopt;
    }
    return snapshot;
  }

  if (std::regex_match(trimmed, match, kAvrLine)) {
    snapshot.kind = BenchmarkSnapshotKind::kAveragePass;
    if (!parse_metrics_match(match, false, &snapshot.metrics)) {
      return std::nullopt;
    }
    summary_.has_average_metrics = true;
    summary_.average_metrics = snapshot.metrics;
    return snapshot;
  }

  if (std::regex_match(trimmed, match, kTotLine)) {
    uint64_t usage = 0;
    uint64_t rpu = 0;
    uint64_t rating = 0;
    if (!parse_uint64(match[1].str(), &usage) ||
        !parse_uint64(match[2].str(), &rpu) ||
        !parse_uint64(match[3].str(), &rating)) {
      return std::nullopt;
    }
    snapshot.kind = BenchmarkSnapshotKind::kTotalRating;
    snapshot.total_cpu_usage_percent = usage;
    snapshot.total_rpu_mips = rpu;
    snapshot.total_rating_mips = rating;
    summary_.has_total_rating = true;
    summary_.total_cpu_usage_percent = usage;
    summary_.total_rpu_mips = rpu;
    summary_.total_rating_mips = rating;
    return snapshot;
  }

  if (std::regex_match(trimmed, match, kRamSizeLine)) {
    uint64_t size_mb = 0;
    if (!parse_uint64(match[1].str(), &size_mb)) {
      return std::nullopt;
    }
    snapshot.kind = BenchmarkSnapshotKind::kRamSize;
    snapshot.ram_size_mb = size_mb;
    summary_.has_ram_size = true;
    summary_.ram_size_mb = size_mb;
    return snapshot;
  }

  if (std::regex_match(trimmed, match, kRamUsageLine)) {
    uint64_t usage_mb = 0;
    if (!parse_uint64(match[1].str(), &usage_mb)) {
      return std::nullopt;
    }
    snapshot.kind = BenchmarkSnapshotKind::kRamUsage;
    snapshot.ram_usage_mb = usage_mb;
    summary_.has_ram_usage = true;
    summary_.ram_usage_mb = usage_mb;
    return snapshot;
  }

  if (std::regex_match(trimmed, match, kFrequencyLine)) {
    snapshot.kind = BenchmarkSnapshotKind::kFrequency;
    snapshot.frequency_line = trimmed;
    return snapshot;
  }

  return std::nullopt;
}

const BenchmarkTypedSummary& BenchmarkTypedParser::summary() const {
  return summary_;
}

std::string BenchmarkTypedParser::summary_line() const {
  if (summary_.has_total_rating) {
    return "Tot: " + std::to_string(summary_.total_cpu_usage_percent) + " " +
           std::to_string(summary_.total_rpu_mips) + " " +
           std::to_string(summary_.total_rating_mips);
  }

  if (summary_.has_average_metrics) {
    const BenchmarkTypedMetrics& m = summary_.average_metrics;
    return "Avr: " + std::to_string(m.compress_speed_kb_per_sec) + " " +
           std::to_string(m.compress_cpu_usage_percent) + " " +
           std::to_string(m.compress_rpu_mips) + " " +
           std::to_string(m.compress_rating_mips) + " | " +
           std::to_string(m.decompress_speed_kb_per_sec) + " " +
           std::to_string(m.decompress_cpu_usage_percent) + " " +
           std::to_string(m.decompress_rpu_mips) + " " +
           std::to_string(m.decompress_rating_mips);
  }

  return {};
}

}  // namespace z7::app
