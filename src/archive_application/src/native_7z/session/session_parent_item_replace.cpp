// src/archive_application/src/native_7z/session/session_parent_item_replace.cpp
// Role: Replace one exact parent archive item without path-based ambiguity.

#include "session/session_parent_item_replace.h"

#include <memory>
#include <string>
#include <utility>

#include "core/filesystem_replace.h"
#include "core/internal.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {

    namespace {

        UpdateOperationStatus collect_update_status(HRESULT hresult,
                                                    NativeUpdateOperationCallback const& callback) {
            UpdateOperationStatus status;
            status.hresult = hresult;
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
            status.open_diagnostics.error_count = callback.open_error_count();
            status.open_diagnostics.error_message = callback.open_error_message();
            return status;
        }

        std::optional<std::string> discard_staged_output(FilesystemTransaction& transaction,
                                                         fs::path const& staged_path) {
            std::string diagnostic;
            std::error_code exists_ec;
            if (fs::exists(staged_path, exists_ec)) {
                TransactionMoveResult const discarded = transaction.discard(staged_path);
                if (!discarded.success) {
                    diagnostic = discarded.diagnostic;
                }
            } else if (exists_ec) {
                diagnostic = "Failed to inspect staged archive output: " + exists_ec.message();
            }

            std::string finish_diagnostic;
            if (!transaction.finish(&finish_diagnostic)) {
                if (!diagnostic.empty()) {
                    diagnostic += "; ";
                }
                diagnostic += finish_diagnostic;
            }
            return diagnostic.empty() ? std::nullopt : std::optional<std::string>(std::move(diagnostic));
        }

    } // namespace

    std::optional<OperationResult>
    validate_archive_session_parent_item_replacement(ArchiveOpenSession const& parent) {
        ArchiveOpenSessionState const& state = archive_session_state(parent);
        if (state.archive_link == nullptr || state.codecs == nullptr || !state.archive_link->IsOpen
            || state.archive_link->Arcs.IsEmpty()) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Parent archive is unavailable for nested item replacement", 2);
        }

        CArchiveLink const& link = *state.archive_link;
        if (link.VolumePaths.Size() > 1) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested archive writeback is not supported for multi-volume parent archives",
                2);
        }
        if (link.Arcs.Size() != 1) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested archive writeback is not supported for multi-layer parent archives",
                2);
        }

        CArc const& arc = link.Arcs[0];
        if (arc.ErrorInfo.ThereIsTail) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested archive writeback is not supported when the parent archive has trailing data",
                2);
        }
        if (arc.IsReadOnly) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested archive writeback is not supported for read-only parent archives",
                2);
        }
        if (arc.FormatIndex < 0 || static_cast<unsigned>(arc.FormatIndex) >= state.codecs->Formats.Size()
            || !state.codecs->Formats[static_cast<unsigned>(arc.FormatIndex)].UpdateEnabled) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested archive writeback is not supported by the parent archive format",
                2);
        }
        if (arc.Archive == nullptr) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Parent archive is unavailable for nested item replacement", 2);
        }
        return std::nullopt;
    }

    OperationResult replace_archive_session_item_by_index(ArchiveOpenSession& parent,
                                                           uint32_t parent_entry_index,
                                                           std::filesystem::path const& replacement_path,
                                                           ArchiveBackendHooks const& hooks,
                                                           std::atomic<bool>* cancel_requested,
                                                           std::function<bool()> wait_while_paused) {
        if (std::optional<OperationResult> validation_error =
                validate_archive_session_parent_item_replacement(parent);
            validation_error.has_value()) {
            return std::move(*validation_error);
        }

        ArchiveOpenSessionState& parent_state = archive_session_state(parent);
        CArc const* arc = parent_state.archive_link == nullptr ? nullptr : parent_state.archive_link->GetArc();
        if (arc == nullptr || arc->Archive == nullptr || parent_state.temp_file == nullptr
            || parent_state.temp_file->empty()) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Parent session is not writable for exact item replacement", 2);
        }

        UInt32 num_items = 0;
        HRESULT const count_hr = arc->Archive->GetNumberOfItems(&num_items);
        if (count_hr != S_OK) {
            return make_operation_failure_from_hresult<OperationResult>(count_hr);
        }
        if (parent_entry_index >= num_items) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Parent entry index is out of range for replacement", 7);
        }

        std::error_code replacement_ec;
        if (!fs::is_regular_file(replacement_path, replacement_ec) || replacement_ec) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments,
                "Nested archive replacement path is not a regular file"
                    + (replacement_ec ? std::string(": ") + replacement_ec.message() : ""),
                7);
        }

        CDirItems dir_items;
        FStringVector file_paths;
        file_paths.Add(us2fs(utf8_to_ustring(replacement_path.string())));
        HRESULT const enumerate_hr = dir_items.EnumerateItems2(FString(), UString(), file_paths, nullptr);
        if (enumerate_hr != S_OK) {
            return make_operation_failure_from_hresult<OperationResult>(enumerate_hr);
        }
        if (dir_items.Items.Size() != 1) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Nested archive replacement did not enumerate exactly one file", 2);
        }

        CMyComPtr<IOutArchive> out_archive;
        HRESULT const query_out_hr = arc->Archive->QueryInterface(IID_IOutArchive, (void**)&out_archive);
        if (query_out_hr != S_OK || !out_archive) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat, "Parent archive format does not support item replacement", 2);
        }

        CRecordVector<CUpdatePair2> update_pairs;
        update_pairs.Reserve(num_items);
        for (UInt32 index = 0; index < num_items; ++index) {
            CUpdatePair2 pair;
            pair.SetAs_NoChangeArcItem(index);
            if (index == parent_entry_index) {
                pair.DirIndex = 0;
                pair.NewData = true;
                pair.NewProps = true;
                pair.UseArcProps = false;
            }
            update_pairs.Add(pair);
        }

        std::atomic<bool> local_cancel_requested{false};
        std::atomic<bool>* const effective_cancel =
            cancel_requested == nullptr ? &local_cancel_requested : cancel_requested;
        if (!wait_while_paused) {
            wait_while_paused = []() { return true; };
        }
        NativeUpdateOperationCallback operation_callback(
            hooks,
            effective_cancel,
            std::move(wait_while_paused),
            parent.display_path(),
            NativeUpdateOperationCallback::Mode::kAdd,
            OpenResultMessagePolicy::kOperationMessages,
            parent.password_defined() ? parent.password() : std::string{},
            /*reject_open_errors=*/true);
        operation_callback.set_total_files_hint(1);

        std::error_code transaction_ec;
        std::unique_ptr<FilesystemTransaction> output_transaction =
            FilesystemTransaction::create(*parent_state.temp_file, "session-parent-replace", transaction_ec);
        if (!output_transaction) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Failed to create exact-replacement transaction"
                    + (transaction_ec ? std::string(": ") + transaction_ec.message() : ""),
                2);
        }
        fs::path const staged_output = output_transaction->allocate_path("archive");
        COutFileStream* out_stream_spec = new COutFileStream();
        CMyComPtr<IOutStream> out_stream(out_stream_spec);
        if (!out_stream_spec->Create_ALWAYS(us2fs(utf8_to_ustring(staged_output.string())))) {
            std::optional<std::string> const cleanup = discard_staged_output(*output_transaction, staged_output);
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                "Failed to create exact-replacement archive output"
                    + (cleanup.has_value() ? std::string("; ") + *cleanup : ""),
                2);
        }

        CMyComPtr<ISequentialOutStream> update_stream;
        HRESULT setup_hr = S_OK;
        if (arc->ArcStreamOffset == 0) {
            update_stream = out_stream;
        } else if (!arc->InStream) {
            setup_hr = E_NOTIMPL;
        } else {
            setup_hr = arc->InStream->Seek(0, STREAM_SEEK_SET, nullptr);
            if (setup_hr == S_OK) {
                setup_hr = NCompress::CopyStream_ExactSize(
                    arc->InStream, out_stream, arc->ArcStreamOffset, nullptr);
            }
            if (setup_hr == S_OK) {
                CTailOutStream* tail_stream_spec = new CTailOutStream;
                CMyComPtr<IOutStream> tail_stream(tail_stream_spec);
                tail_stream_spec->Stream = out_stream;
                tail_stream_spec->Offset = arc->ArcStreamOffset;
                tail_stream_spec->Init();
                update_stream = tail_stream;
            }
        }

        HRESULT update_hr = setup_hr;
        if (update_hr == S_OK) {
            CMyComPtr2_Create<IArchiveUpdateCallback, CArchiveUpdateCallback> update_callback;
            update_callback->DirItems = &dir_items;
            update_callback->Callback = &operation_callback;
            update_callback->UpdatePairs = &update_pairs;
            update_callback->Arc = arc;
            update_callback->Archive = arc->Archive;
            update_callback->ArcFileName = utf8_to_ustring(parent_state.temp_file->filename().string());
            update_callback->KeepOriginalItemNames = true;
            update_hr = out_archive->UpdateItems(
                update_stream, static_cast<UInt32>(update_pairs.Size()), update_callback);
        }
        HRESULT const close_hr = out_stream_spec->Close();
        if (update_hr == S_OK && close_hr != S_OK) {
            update_hr = close_hr;
        }

        UpdateOperationStatus const update_status = collect_update_status(update_hr, operation_callback);
        out_archive.Release();
        parent_state.archive_link.reset();
        parent_state.stream_ref_holder.reset();

        if (update_hr != S_OK || effective_cancel->load() || update_status.error_count != 0
            || update_status.password_requested || update_status.wrong_password) {
            OperationResult failure =
                finalize_update_operation_result<OperationResult>(hooks, *effective_cancel, update_status);
            if (std::optional<std::string> const cleanup =
                    discard_staged_output(*output_transaction, staged_output);
                cleanup.has_value()) {
                attach_error_message(&failure,
                                     failure.error.message.empty() ? *cleanup
                                                                   : failure.error.message + "; " + *cleanup);
            }
            return failure;
        }

        AtomicReplaceResult const replace_result = replace_file_atomically(
            staged_output, *parent_state.temp_file, ".z7-session-parent-replace-");
        if (!replace_result.success) {
            (void)discard_staged_output(*output_transaction, staged_output);
            return replace_result.error.value_or(make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo, "Failed to commit exact parent-item replacement", 2));
        }

        std::string finish_diagnostic;
        if (!output_transaction->finish(&finish_diagnostic)) {
            emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kStdErr, finish_diagnostic);
        }
        return finalize_update_operation_result<OperationResult>(
            hooks, *effective_cancel, update_status, "Nested archive item replaced");
    }

} // namespace z7::app
