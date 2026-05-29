// src/ui/runtime_support/src/platform_support.cpp
// Role: Platform-specific reason key and display helpers.

#include "platform_support.h"

namespace z7::ui::runtime_support {
namespace {

QString platform_name(PlatformSupport support) {
  switch (support) {
    case PlatformSupport::kWindowsOnly:
      return QStringLiteral("Windows");
    case PlatformSupport::kMacOnly:
      return QStringLiteral("macOS");
    case PlatformSupport::kLinuxOnly:
      return QStringLiteral("Linux");
  }
  return QStringLiteral("Unknown");
}

}  // namespace

bool is_platform_supported(PlatformSupport support) {
  switch (support) {
    case PlatformSupport::kWindowsOnly:
#if defined(Q_OS_WIN)
      return true;
#else
      return false;
#endif
    case PlatformSupport::kMacOnly:
#if defined(Q_OS_MACOS)
      return true;
#else
      return false;
#endif
    case PlatformSupport::kLinuxOnly:
#if defined(Q_OS_LINUX)
      return true;
#else
      return false;
#endif
  }
  return false;
}

QString platform_reason_key(PlatformSupport support) {
  switch (support) {
    case PlatformSupport::kWindowsOnly:
      return QStringLiteral("WindowsOnly");
    case PlatformSupport::kMacOnly:
      return QStringLiteral("MacOnly");
    case PlatformSupport::kLinuxOnly:
      return QStringLiteral("LinuxOnly");
  }
  return QStringLiteral("UnknownPlatform");
}

QString platform_suffix(PlatformSupport support) {
  return QStringLiteral(" (%1)").arg(platform_name(support));
}

QString platform_tooltip(PlatformSupport support) {
  return QStringLiteral("%1 only").arg(platform_name(support));
}

QString with_platform_suffix_if_unsupported(const QString& base_text,
                                            PlatformSupport support) {
  if (is_platform_supported(support)) {
    return base_text;
  }
  return base_text + platform_suffix(support);
}

PlatformRestrictionUi platform_restriction_ui(const QString& base_text,
                                              PlatformSupport support) {
  const bool supported = is_platform_supported(support);
  PlatformRestrictionUi out;
  out.supported = supported;
  out.text = supported ? base_text : base_text + platform_suffix(support);
  out.reason_key = supported ? QString() : platform_reason_key(support);
  out.tooltip = supported ? QString() : platform_tooltip(support);
  return out;
}

}  // namespace z7::ui::runtime_support
