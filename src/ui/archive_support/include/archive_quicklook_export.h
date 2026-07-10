#pragma once

#include <QString>

#include "archive_types_base.h"
#include "archive_types_extract.h"

namespace z7::ui::archive_support {

    struct QuickLookExtractPlan {
        QString output_dir;
        z7::app::ExtractPathRemap path_remap;
    };

    QuickLookExtractPlan build_quicklook_extract_plan(QString const& normalized_entry_path,
                                                      bool entry_is_directory,
                                                      QString const& destination_path);

} // namespace z7::ui::archive_support
