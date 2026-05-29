#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace z7::app {

enum class BenchmarkSnapshotKind {
  kUnknown,
  kDictionaryPass,
  kAveragePass,
  kTotalRating,
  kRamSize,
  kRamUsage,
  kFrequency
};

struct BenchmarkTypedMetrics {
  bool has_dictionary_log = false;
  uint32_t dictionary_log = 0;
  bool has_compress_size = false;
  uint64_t compress_size_bytes = 0;
  uint64_t compress_speed_kb_per_sec = 0;
  uint64_t compress_cpu_usage_percent = 0;
  uint64_t compress_rpu_mips = 0;
  uint64_t compress_rating_mips = 0;
  bool has_decompress_size = false;
  uint64_t decompress_size_bytes = 0;
  uint64_t decompress_speed_kb_per_sec = 0;
  uint64_t decompress_cpu_usage_percent = 0;
  uint64_t decompress_rpu_mips = 0;
  uint64_t decompress_rating_mips = 0;
};

struct BenchmarkTypedSnapshot {
  BenchmarkSnapshotKind kind = BenchmarkSnapshotKind::kUnknown;
  BenchmarkTypedMetrics metrics;
  uint64_t total_cpu_usage_percent = 0;
  uint64_t total_rpu_mips = 0;
  uint64_t total_rating_mips = 0;
  uint64_t ram_size_mb = 0;
  uint64_t ram_usage_mb = 0;
  std::string frequency_line;
};

struct BenchmarkTypedSummary {
  bool has_average_metrics = false;
  BenchmarkTypedMetrics average_metrics;
  bool has_total_rating = false;
  uint64_t total_cpu_usage_percent = 0;
  uint64_t total_rpu_mips = 0;
  uint64_t total_rating_mips = 0;
  bool has_ram_size = false;
  uint64_t ram_size_mb = 0;
  bool has_ram_usage = false;
  uint64_t ram_usage_mb = 0;
};

struct BenchmarkRequest {
  // 0 means backend default iteration count.
  uint32_t iterations = 0;
  std::string thread_count;
  std::string dictionary_size;
  std::string method_value;
  bool total_mode = false;
};

struct BenchmarkMemoryEstimate {
  bool ok = false;
  uint64_t estimated_usage_bytes = 0;
  uint64_t total_ram_bytes = 0;
  uint64_t safe_ram_limit_bytes = 0;
  bool ram_defined = false;
  bool within_limit = true;
};

struct BenchmarkSystemInfo {
  uint32_t process_threads = 1;
  uint32_t system_threads = 1;
  std::string hardware_threads;
  std::string sys1;
  std::string sys2;
  std::string cpu;
  std::string cpu_feature;
  std::string version;
};

}  // namespace z7::app
