#pragma once

#include <cstdint>

#include <QString>
#include <QtGlobal>

namespace z7::ui::runtime_support {

struct ExtractMemoryLimitSettings final {
  bool enabled = false;
  int limit_gb = 4;
};

bool extract_memory_limit_supported();
QString extract_memory_limit_platform_suffix();
QString extract_memory_limit_platform_tooltip();
QString with_extract_memory_limit_platform_suffix_if_unsupported(const QString& text);

quint64 detect_total_ram_bytes();
quint64 rounded_ram_gb(quint64 ram_bytes);
int max_extract_memory_limit_gb(quint64 ram_bytes);
QString format_extract_memory_limit_suffix(quint64 ram_bytes);

int clamp_extract_memory_limit_gb(int limit_gb, quint64 ram_bytes);
uint64_t extract_memory_limit_gb_to_bytes(int limit_gb);
int extract_memory_limit_bytes_to_storage_gb(uint64_t bytes);

ExtractMemoryLimitSettings load_extract_memory_limit_settings();
void save_extract_memory_limit_settings(const ExtractMemoryLimitSettings& settings);
uint64_t configured_extract_memory_limit_bytes();
void save_extract_memory_limit_bytes_and_enable(uint64_t bytes);

uint64_t load_benchmark_memory_limit_bytes();
void save_benchmark_memory_limit_bytes(uint64_t bytes);

}  // namespace z7::ui::runtime_support
