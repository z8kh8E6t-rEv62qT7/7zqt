#pragma once

#include "archive_session.h"
#include "archive_process_runner.h"

namespace z7::ui::filemanager::runner_helpers {

z7::app::OverwriteMode to_backend_overwrite_mode(OverwriteMode mode);

}  // namespace z7::ui::filemanager::runner_helpers
