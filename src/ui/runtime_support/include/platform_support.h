// src/ui/runtime_support/include/platform_support.h
// Role: Shared helpers for platform-exclusive GUI labeling and reason keys.

#pragma once

#include <QString>

namespace z7::ui::runtime_support {

enum class PlatformSupport {
  kWindowsOnly,
  kMacOnly,
  kLinuxOnly
};

struct PlatformRestrictionUi {
  bool supported = false;
  QString text;
  QString reason_key;
  QString tooltip;
};

bool is_platform_supported(PlatformSupport support);
QString platform_reason_key(PlatformSupport support);
QString platform_suffix(PlatformSupport support);
QString platform_tooltip(PlatformSupport support);
QString with_platform_suffix_if_unsupported(const QString& base_text,
                                            PlatformSupport support);
PlatformRestrictionUi platform_restriction_ui(const QString& base_text,
                                              PlatformSupport support);

}  // namespace z7::ui::runtime_support
