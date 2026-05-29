#include "helpers.h"

namespace z7::ui::filemanager::runner_helpers {

z7::app::OverwriteMode to_backend_overwrite_mode(OverwriteMode mode) {
  switch (mode) {
    case OverwriteMode::kAsk:
      return z7::app::OverwriteMode::kAsk;
    case OverwriteMode::kOverwrite:
      return z7::app::OverwriteMode::kOverwrite;
    case OverwriteMode::kSkip:
      return z7::app::OverwriteMode::kSkip;
    case OverwriteMode::kRenameExisting:
      return z7::app::OverwriteMode::kRenameExisting;
    case OverwriteMode::kRenameExtracted:
      return z7::app::OverwriteMode::kRenameExtracted;
  }

  return z7::app::OverwriteMode::kOverwrite;
}

}  // namespace z7::ui::filemanager::runner_helpers
