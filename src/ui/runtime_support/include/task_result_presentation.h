#pragma once

#include "archive_error.h"

namespace z7::ui::runtime_support {

    enum class TaskResultPresentation {
        kNormal,
        kMessages,
        kFinalError,
        kFinalErrorThenMessages,
        kCanceled,
    };

    TaskResultPresentation classify_task_result(bool ok,
                                                 z7::app::ArchiveErrorDomain error_domain,
                                                 bool has_result_messages) noexcept;

} // namespace z7::ui::runtime_support
