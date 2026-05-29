// src/archive_application/src/native_7z/core/event_update.cpp
// Role: Shared update operation execution for native backend operations.

#include "core/internal.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app
{

    UpdateOperationStatus run_update_archive_shared(CCodecs* codecs,
                                                    CObjectVector<COpenType>& types,
                                                    std::string const& archive_path,
                                                    NWildcard::CCensor& censor,
                                                    CUpdateOptions& options,
                                                    CUpdateErrorInfo& error_info,
                                                    NativeUpdateOperationCallback& callback)
    {
        UpdateOperationStatus status;
        status.hresult = UpdateArchive(
            codecs, types, utf8_to_ustring(archive_path), censor, options, error_info, &callback, &callback, true);

        status.totals_known = callback.totals_known();
        status.total_bytes = callback.total_bytes();
        status.completed_bytes = callback.completed_bytes();
        status.total_files = callback.total_files();
        status.completed_files = callback.completed_files();
        status.error_count = callback.error_count();
        status.current_path = callback.current_path();
        status.ratio_info = callback.ratio_info();
        status.password_requested = callback.password_requested();
        status.wrong_password = callback.wrong_password();
        status.diagnostic = update_error_message_to_utf8(error_info);
        return status;
    }

} // namespace z7::app
