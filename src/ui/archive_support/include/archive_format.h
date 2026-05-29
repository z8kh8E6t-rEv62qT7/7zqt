#pragma once

#include <cstdint>

#include <QString>

#include "archive_types_base.h"

namespace z7::ui::archive_support {

QString stage_label_for(z7::app::OperationStage stage);
QString format_binary_size(uint64_t bytes);
QString format_dual_size(uint64_t bytes);

}  // namespace z7::ui::archive_support
