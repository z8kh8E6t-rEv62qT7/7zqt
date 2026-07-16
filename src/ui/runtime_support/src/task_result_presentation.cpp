#include "task_result_presentation.h"

namespace z7::ui::runtime_support {

    TaskResultPresentation classify_task_result(bool ok,
                                                 z7::app::ArchiveErrorDomain error_domain,
                                                 bool has_result_messages) noexcept {
        if (error_domain == z7::app::ArchiveErrorDomain::kCanceled) {
            return TaskResultPresentation::kCanceled;
        }
        if (error_domain == z7::app::ArchiveErrorDomain::kPartialSuccess) {
            return has_result_messages ? TaskResultPresentation::kMessages
                                       : TaskResultPresentation::kFinalError;
        }
        if (has_result_messages) {
            if (!ok) {
                return TaskResultPresentation::kFinalErrorThenMessages;
            }
            return TaskResultPresentation::kMessages;
        }
        return ok ? TaskResultPresentation::kNormal : TaskResultPresentation::kFinalError;
    }

} // namespace z7::ui::runtime_support
