// src/ui/runtime_support/src/extract_memory_settings.cpp
// Role: Shared persisted limits for memory-heavy extraction/test operations.

#include "extract_memory_settings.h"

#include <algorithm>
#include <limits>

#include <QVariant>

#include "portable_settings.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace z7::ui::runtime_support {
namespace {

constexpr const char* kSharedSettingsApp = "7z-shared";
constexpr const char* kBenchmarkMemoryLimitBytesKey = "Benchmark/MemoryLimitBytes";
constexpr const char* kExtractMemoryLimitGbKey = "Extraction/MemLimit";
constexpr quint64 kBytesPerGiB = 1ULL << 30;
constexpr int kDefaultLimitGb = 4;

z7::platform::qt::PortableSettings shared_settings() {
  return z7::platform::qt::PortableSettings(QString(), QString::fromLatin1(kSharedSettingsApp));
}

}  // namespace

bool extract_memory_limit_supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return true;
#else
  return false;
#endif
}

QString extract_memory_limit_platform_suffix() {
  return QStringLiteral(" (Windows/macOS)");
}

QString extract_memory_limit_platform_tooltip() {
  return QStringLiteral("Windows and macOS only");
}

QString with_extract_memory_limit_platform_suffix_if_unsupported(
    const QString& text) {
  if (extract_memory_limit_supported()) {
    return text;
  }
  return text + extract_memory_limit_platform_suffix();
}

quint64 detect_total_ram_bytes() {
#if defined(Q_OS_WIN)
  MEMORYSTATUSEX memory_status = {};
  memory_status.dwLength = sizeof(memory_status);
  if (GlobalMemoryStatusEx(&memory_status)) {
    return static_cast<quint64>(memory_status.ullTotalPhys);
  }
  return 0;
#elif defined(Q_OS_UNIX)
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0) {
    return 0;
  }
  return static_cast<quint64>(pages) * static_cast<quint64>(page_size);
#else
  return 0;
#endif
}

quint64 rounded_ram_gb(quint64 ram_bytes) {
  if (ram_bytes == 0) {
    return 0;
  }
  return (ram_bytes + (kBytesPerGiB - 1)) >> 30;
}

int max_extract_memory_limit_gb(quint64 ram_bytes) {
  constexpr int kDefaultMax = 64;
  constexpr int kHardMax = 1 << 14;

  const quint64 ram_gb = rounded_ram_gb(ram_bytes);
  if (ram_gb <= 1) {
    return 1;
  }
  if (ram_gb >= static_cast<quint64>(kHardMax + 1)) {
    return kHardMax;
  }
  const quint64 max_by_ram = ram_gb - 1;
  if (max_by_ram == 0) {
    return 1;
  }
  return static_cast<int>(std::min<quint64>(max_by_ram, kDefaultMax));
}

QString format_extract_memory_limit_suffix(quint64 ram_bytes) {
  const quint64 ram_gb = rounded_ram_gb(ram_bytes);
  if (ram_gb == 0) {
    return QStringLiteral("GB");
  }
  return QStringLiteral("GB / %1 GB (RAM)").arg(ram_gb);
}

int clamp_extract_memory_limit_gb(int limit_gb, quint64 ram_bytes) {
  const int max_gb = max_extract_memory_limit_gb(ram_bytes);
  return std::clamp(limit_gb, 1, max_gb);
}

uint64_t extract_memory_limit_gb_to_bytes(int limit_gb) {
  if (limit_gb <= 0) {
    return 0;
  }
  return static_cast<uint64_t>(limit_gb) * static_cast<uint64_t>(kBytesPerGiB);
}

int extract_memory_limit_bytes_to_storage_gb(uint64_t bytes) {
  if (bytes == 0) {
    return 0;
  }
  const uint64_t gb = (bytes + static_cast<uint64_t>(kBytesPerGiB) - 1) /
                      static_cast<uint64_t>(kBytesPerGiB);
  constexpr uint64_t kMaxInt = static_cast<uint64_t>(std::numeric_limits<int>::max());
  return static_cast<int>(std::min<uint64_t>(gb, kMaxInt));
}

ExtractMemoryLimitSettings load_extract_memory_limit_settings() {
  z7::platform::qt::PortableSettings settings;
  const quint64 ram_bytes = detect_total_ram_bytes();
  const int default_limit = clamp_extract_memory_limit_gb(kDefaultLimitGb, ram_bytes);

  ExtractMemoryLimitSettings out;
  out.enabled = settings.contains(QString::fromLatin1(kExtractMemoryLimitGbKey));
  out.limit_gb =
      clamp_extract_memory_limit_gb(
          settings.value(QString::fromLatin1(kExtractMemoryLimitGbKey),
                         default_limit)
              .toInt(),
          ram_bytes);
  return out;
}

void save_extract_memory_limit_settings(
    const ExtractMemoryLimitSettings& settings_value) {
  const quint64 ram_bytes = detect_total_ram_bytes();
  z7::platform::qt::PortableSettings settings;
  if (!settings_value.enabled) {
    settings.remove(QString::fromLatin1(kExtractMemoryLimitGbKey));
    settings.sync();
    return;
  }
  settings.setValue(QString::fromLatin1(kExtractMemoryLimitGbKey),
                    clamp_extract_memory_limit_gb(settings_value.limit_gb, ram_bytes));
  settings.sync();
}

uint64_t configured_extract_memory_limit_bytes() {
  if (!extract_memory_limit_supported()) {
    return 0;
  }
  const ExtractMemoryLimitSettings settings = load_extract_memory_limit_settings();
  if (!settings.enabled) {
    return 0;
  }
  return extract_memory_limit_gb_to_bytes(settings.limit_gb);
}

void save_extract_memory_limit_bytes_and_enable(uint64_t bytes) {
  if (!extract_memory_limit_supported() || bytes == 0) {
    return;
  }
  ExtractMemoryLimitSettings settings;
  settings.enabled = true;
  settings.limit_gb = extract_memory_limit_bytes_to_storage_gb(bytes);
  save_extract_memory_limit_settings(settings);
}

uint64_t load_benchmark_memory_limit_bytes() {
  z7::platform::qt::PortableSettings settings = shared_settings();
  return settings
      .value(QString::fromLatin1(kBenchmarkMemoryLimitBytesKey),
             QVariant::fromValue<qulonglong>(0))
      .toULongLong();
}

void save_benchmark_memory_limit_bytes(uint64_t bytes) {
  z7::platform::qt::PortableSettings settings = shared_settings();
  settings.setValue(QString::fromLatin1(kBenchmarkMemoryLimitBytesKey),
                    QVariant::fromValue<qulonglong>(bytes));
  settings.sync();
}

}  // namespace z7::ui::runtime_support
