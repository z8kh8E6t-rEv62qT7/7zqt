#pragma once

#include "internal.h"

namespace z7::macos_integration::capi_internal {

std::shared_ptr<z7::app::IArchiveDelegate> make_quicklook_password_relay_delegate(
    const std::shared_ptr<z7_mi_session_state>& state,
    const QString& archive_path_abs,
    const QString& effective_archive_type,
    const QStringList& nested_chain,
    const std::shared_ptr<AsyncTaskState>& task_state,
    std::shared_ptr<z7::app::IArchiveDelegate> forward = {});

}  // namespace z7::macos_integration::capi_internal
