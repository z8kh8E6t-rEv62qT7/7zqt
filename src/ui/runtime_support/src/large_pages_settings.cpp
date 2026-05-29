// src/ui/runtime_support/src/large_pages_settings.cpp
// Role: Shared persisted setting for platform large-page allocator mode.

#include "large_pages_settings.h"

#include <QtGlobal>

#include "archive_large_pages.h"
#include "portable_settings.h"

namespace z7::ui::runtime_support {
namespace {

constexpr const char* kSettingsAppName = "7zFM";
constexpr const char* kLargePagesKey = "LargePages";

z7::platform::qt::PortableSettings app_settings() {
  return z7::platform::qt::PortableSettings(QString(),
                                            QString::fromLatin1(kSettingsAppName));
}

}  // namespace

bool large_pages_supported() {
#if defined(Q_OS_MACOS)
  return z7::app::large_pages_runtime_supported();
#else
  return false;
#endif
}

QString large_pages_platform_suffix() {
  return QStringLiteral(" (macOS)");
}

QString large_pages_platform_tooltip() {
  return QStringLiteral("macOS only");
}

QString with_large_pages_platform_suffix_if_unsupported(const QString& text) {
  if (large_pages_supported()) {
    return text;
  }
  return text + large_pages_platform_suffix();
}

bool load_large_pages_enabled() {
  if (!large_pages_supported()) {
    return false;
  }
  const z7::platform::qt::PortableSettings settings = app_settings();
  return settings.value(QString::fromLatin1(kLargePagesKey), false).toBool();
}

void save_large_pages_enabled(bool enabled) {
  if (!large_pages_supported()) {
    return;
  }
  z7::platform::qt::PortableSettings settings = app_settings();
  settings.setValue(QString::fromLatin1(kLargePagesKey), enabled);
  settings.sync();
}

bool probe_large_pages_runtime() {
  return large_pages_supported() && z7::app::probe_large_pages_runtime();
}

void apply_configured_large_pages_mode() {
  z7::app::set_large_pages_runtime_enabled(load_large_pages_enabled());
}

}  // namespace z7::ui::runtime_support
