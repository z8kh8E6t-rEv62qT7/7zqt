// src/ui/gui/src/gui_task_runner_helpers.cpp
// Role: Misc shared helper utilities for GuiTaskRunner.

#include "gui_task_runner_helpers.h"

#include <QDir>

#include "archive_string_codec_qt.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui {
namespace {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

}  // namespace

QString progress_header_for_spec(const GuiTaskSpec& spec,
                                 const QString& base_title) {
  return std::visit(
      [&base_title](const auto& typed_spec) {
        using T = std::decay_t<decltype(typed_spec)>;
        if constexpr (std::is_same_v<T, TestTaskSpec>) {
          const QString testing = strip_mnemonic(L(3302));
          if (typed_spec.archive_inputs.empty()) {
            return testing;
          }
          const QString first_path = QDir::toNativeSeparators(
              z7::ui::archive_support::from_native_string(
                  typed_spec.archive_inputs.front()));
          if (first_path.trimmed().isEmpty()) {
            return testing;
          }
          return QStringLiteral("%1: %2").arg(testing, first_path);
        } else if constexpr (std::is_same_v<T, ArchiveTestTaskSpec>) {
          const QString testing = strip_mnemonic(L(3302));
          const QString archive_path = QDir::toNativeSeparators(
              z7::ui::archive_support::from_native_string(
                  typed_spec.archive_path));
          if (archive_path.trimmed().isEmpty()) {
            return testing;
          }
          return QStringLiteral("%1: %2").arg(testing, archive_path);
        } else {
          return base_title;
        }
      },
      spec);
}

}  // namespace z7::ui::gui
