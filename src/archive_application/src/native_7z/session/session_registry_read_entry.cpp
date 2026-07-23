// src/archive_application/src/native_7z/session/session_registry_read_entry.cpp
// Role: Bounded, disk-free reads of one entry from an open archive session.

#include <limits>
#include <mutex>

#include "core/internal.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_extract_stream.h"

namespace z7::app {

    ReadArchiveEntryResult NativeArchiveBackend::read_archive_entry(ReadArchiveEntryRequest const& request,
                                                                    ArchiveBackendHooks const& hooks) {
        auto session = ArchiveSessionRegistry::instance().find(request.session_token);
        if (!session) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }

        std::unique_lock<std::recursive_mutex> session_lock(
            ArchiveOpenSessionNativeAccess::operation_mutex(*session));
        ScopedFilenameCodePage filename_scope(session->filename_code_page());
        if (ArchiveOpenSessionNativeAccess::closed(*session)) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Archive session is already closed", 7);
        }

        CArc const* arc = archive_session_link(*session).GetArc();
        if (arc == nullptr || arc->Archive == nullptr) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Session archive unavailable", 7);
        }

        UInt32 num_items = 0;
        HRESULT const count_result = arc->Archive->GetNumberOfItems(&num_items);
        if (count_result != S_OK) {
            return make_operation_failure_from_hresult<ReadArchiveEntryResult>(count_result);
        }
        if (request.entry_index >= num_items) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Archive entry index is out of range", 7);
        }

        bool is_directory = false;
        if (archive_get_prop_bool(arc->Archive, request.entry_index, kpidIsDir, is_directory) && is_directory) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Archive entry is a directory", 7);
        }

        std::optional<uint64_t> const declared_size = archive_declared_entry_size(*arc, request.entry_index);
        if (declared_size.has_value() && *declared_size > request.max_bytes) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kBudgetExceeded, "Archive entry exceeds the in-memory read limit", 2);
        }
        if (request.max_bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kInvalidArguments, "Archive entry read limit is not representable", 7);
        }

        auto buffer = std::make_shared<std::vector<uint8_t>>();
        if (declared_size.has_value()) {
            buffer->reserve(static_cast<size_t>(*declared_size));
        }
        CMyComPtr<NativeBufferOutStream> output;
        output.Attach(new NativeBufferOutStream(*buffer, static_cast<size_t>(request.max_bytes)));
        ExtractInvocationStatus const status = extract_archive_session_entry_to_stream(
            *session,
            request.entry_index,
            output.Interface(),
            hooks,
            &cancel_requested_,
            [this]() { return wait_while_paused(); });

        if (cancel_requested_.load() || status.hresult == E_ABORT) {
            return make_operation_canceled<ReadArchiveEntryResult>();
        }
        if (status.password_requested || status.wrong_password) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
        }
        if (status.hresult == E_OUTOFMEMORY || buffer->size() > request.max_bytes) {
            return make_operation_failure<ReadArchiveEntryResult>(
                ArchiveErrorDomain::kBudgetExceeded, "Archive entry exceeds the in-memory read limit", 2);
        }
        if (status.hresult != S_OK || status.error_count != 0) {
            if (!status.diagnostic.empty()) {
                return make_operation_failure<ReadArchiveEntryResult>(
                    ArchiveErrorDomain::kIo, status.diagnostic, 2);
            }
            return make_operation_failure_from_hresult<ReadArchiveEntryResult>(status.hresult);
        }

        ReadArchiveEntryResult result = make_operation_success<ReadArchiveEntryResult>("Archive entry read");
        result.bytes = std::move(buffer);
        return result;
    }

} // namespace z7::app
