// src/archive_application/src/native_7z/core/event_update.cpp
// Role: Shared update operation execution for native backend operations.

#include <algorithm>
#include <cctype>

#include "core/internal.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {

    namespace {

        std::string find_existing_archive_volume(std::string const& archive_path) {
            fs::path const base(archive_path);
            fs::path const parent = base.has_parent_path() ? base.parent_path() : fs::path(".");
            std::string const prefix = base.filename().string() + '.';
            std::error_code ec;
            for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
                std::string const filename = it->path().filename().string();
                if (filename.size() <= prefix.size() || filename.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }
                std::string const suffix = filename.substr(prefix.size());
                if (suffix.size() < 3
                    || !std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    continue;
                }
                return it->path().string();
            }
            return {};
        }

    } // namespace

    UpdateOperationStatus run_update_archive_shared(CCodecs* codecs,
                                                    CObjectVector<COpenType>& types,
                                                    std::string const& archive_path,
                                                    NWildcard::CCensor& censor,
                                                    CUpdateOptions& options,
                                                    CUpdateErrorInfo& error_info,
                                                    NativeUpdateOperationCallback& callback) {
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
        if (status.diagnostic.empty()
            && status.hresult == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)
            && options.VolumesSizes.Size() != 0) {
            status.diagnostic = "The archive volume already exists";
            std::string const existing_volume = find_existing_archive_volume(archive_path);
            if (!existing_volume.empty()) {
                status.diagnostic += ": " + existing_volume;
            }
        }
        return status;
    }

} // namespace z7::app
