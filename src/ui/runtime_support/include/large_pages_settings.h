#pragma once

#include <QString>

namespace z7::ui::runtime_support {

bool large_pages_supported();
QString large_pages_platform_suffix();
QString large_pages_platform_tooltip();
QString with_large_pages_platform_suffix_if_unsupported(const QString& text);

bool load_large_pages_enabled();
void save_large_pages_enabled(bool enabled);
bool probe_large_pages_runtime();
void apply_configured_large_pages_mode();

}  // namespace z7::ui::runtime_support
