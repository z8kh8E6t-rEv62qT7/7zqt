// src/ui/archive_support/src/archive_quicklook_export.cpp
// Role: Shared Quick Look extract routing helpers for Qt and macOS flows.

#include "archive_quicklook_export.h"

#include <QFileInfo>

#include "archive_string_codec_qt.h"

namespace z7::ui::archive_support {

QuickLookExtractPlan build_quicklook_extract_plan(
    const QString& normalized_entry_path,
    bool entry_is_directory,
    const QString& destination_path) {
  QuickLookExtractPlan plan;
  const QString absolute_destination =
      QFileInfo(destination_path).absoluteFilePath();
  plan.output_dir = QFileInfo(absolute_destination).absolutePath();
  plan.path_remap.destination_path = to_native_string(absolute_destination);

  if (normalized_entry_path.isEmpty()) {
    plan.path_remap.match_kind = z7::app::ExtractPathRemapMatchKind::kRequestRoot;
    return plan;
  }

  plan.path_remap.source_path = to_utf8_string(normalized_entry_path);
  plan.path_remap.match_kind = entry_is_directory
                                   ? z7::app::ExtractPathRemapMatchKind::kArchivePrefix
                                   : z7::app::ExtractPathRemapMatchKind::kExactArchivePath;
  return plan;
}

}  // namespace z7::ui::archive_support
