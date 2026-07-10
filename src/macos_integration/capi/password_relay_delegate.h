#pragma once

#include "internal.h"

namespace z7::macos_integration::capi_internal {

    std::shared_ptr<z7::app::IArchiveDelegate>
    make_quicklook_password_relay_delegate(std::shared_ptr<z7_mi_session_state> const& state,
                                           QString const& archive_path_abs,
                                           QString const& effective_archive_type,
                                           QStringList const& nested_chain,
                                           std::shared_ptr<AsyncTaskState> const& task_state,
                                           std::shared_ptr<z7::app::IArchiveDelegate> forward = {});

} // namespace z7::macos_integration::capi_internal
