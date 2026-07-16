// src/archive_application/src/native_7z/callbacks/callbacks_extract_memory.cpp
// Role: Extract callback memory-limit interaction handling.

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {
    namespace {

        uint64_t bytes_to_gib_ceil(uint64_t bytes) {
            constexpr uint64_t kGiB = uint64_t{1} << 30;
            return bytes == 0 ? 0 : 1 + ((bytes - 1) / kGiB);
        }

        std::string format_memory_error(ExtractMemoryRequest const& request,
                                        UInt64 allowed_size,
                                        bool archive_was_skipped) {
            std::string message = "ERROR: The operation requires big amount of memory (RAM).";
            message += "\n    " + std::to_string(bytes_to_gib_ceil(request.required_size))
                     + " GB : required memory usage size";
            message += "\n    " + std::to_string(bytes_to_gib_ceil(allowed_size))
                     + " GB : allowed memory usage limit";
            if (!request.file_path.empty()) {
                message += "\nFile: " + request.file_path;
            }
            if (archive_was_skipped) {
                message += "\nArchive extraction was skipped.";
            }
            return message;
        }

    } // namespace

    ExtractMemoryResolution resolve_extract_memory_request(ExtractMemoryRequest const& request,
                                                            ArchiveBackendHooks const& hooks) {
        ExtractMemoryResolution resolution;
        resolution.allowed_size = request.allowed_size;
        resolution.answer_flags = request.answer_flags;

        bool const report_only = (request.flags & NRequestMemoryUseFlags::k_IsReport) != 0;
        bool const skip_supported = (request.flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) != 0;
        if (!report_only && request.configured_limit_defined) {
            resolution.allowed_size = request.configured_limit_bytes;
        }

        if (!report_only) {
            if (request.required_size <= resolution.allowed_size) {
                resolution.answer_flags = NRequestMemoryAnswerFlags::k_Allow;
                return resolution;
            }
            resolution.answer_flags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
            if (skip_supported) {
                resolution.answer_flags |= NRequestMemoryAnswerFlags::k_SkipArc;
            }
        }

        if (!report_only && hooks.ask_memory_limit) {
            MemoryLimitPrompt prompt;
            prompt.required_usage_bytes = request.required_size;
            prompt.current_limit_bytes = resolution.allowed_size;
            prompt.current_limit_defined = resolution.allowed_size != 0;
            prompt.archive_path = request.archive_path;
            prompt.file_path = request.file_path;
            prompt.test_mode = request.test_mode;
            prompt.skip_archive_supported = skip_supported;
            prompt.report_only = false;

            MemoryLimitReply reply;
            try {
                reply = hooks.ask_memory_limit(prompt);
            } catch (...) {
                resolution.hresult = E_FAIL;
                return resolution;
            }

            switch (reply.action) {
                case MemoryLimitAction::kAllowOnce:
                    resolution.answer_flags = NRequestMemoryAnswerFlags::k_Allow;
                    return resolution;
                case MemoryLimitAction::kUpdateLimitAndContinue:
                    if (reply.updated_limit_bytes != 0) {
                        resolution.allowed_size = reply.updated_limit_bytes;
                        if (request.required_size <= reply.updated_limit_bytes) {
                            resolution.answer_flags = NRequestMemoryAnswerFlags::k_Allow;
                            return resolution;
                        }
                    }
                    break;
                case MemoryLimitAction::kSkipOperation:
                    if (skip_supported) {
                        resolution.answer_flags =
                            NRequestMemoryAnswerFlags::k_SkipArc | NRequestMemoryAnswerFlags::k_Limit_Exceeded;
                        break;
                    }
                    resolution.answer_flags = NRequestMemoryAnswerFlags::k_Stop;
                    resolution.hresult = E_ABORT;
                    return resolution;
                case MemoryLimitAction::kCancelOperation:
                    resolution.answer_flags = NRequestMemoryAnswerFlags::k_Stop;
                    resolution.hresult = E_ABORT;
                    return resolution;
            }
        }

        resolution.handled_skip = (resolution.answer_flags & NRequestMemoryAnswerFlags::k_SkipArc) != 0;
        if ((request.flags & NRequestMemoryUseFlags::k_NoErrorMessage) == 0) {
            resolution.report_error = true;
            bool const report_skip = skip_supported
                                  || (request.flags & NRequestMemoryUseFlags::k_Report_SkipArc) != 0
                                  || resolution.handled_skip;
            resolution.error_message = format_memory_error(request, resolution.allowed_size, report_skip);
        }
        return resolution;
    }

    ExtractMemoryResolution handle_extract_memory_request(ExtractMemoryRequest const& request,
                                                           ArchiveBackendHooks const& hooks,
                                                           ExtractMemoryCallbackState state) {
        ExtractMemoryResolution resolution = resolve_extract_memory_request(request, hooks);
        if (resolution.handled_skip) {
            state.skip_handled.store(true);
        }
        if (!resolution.report_error || state.error_reported.exchange(true)) {
            return resolution;
        }

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.diagnostic_message.empty()) {
                state.diagnostic_message += '\n';
            }
            state.diagnostic_message += resolution.error_message;
            ++state.error_count;
        }
        emit_archive_scoped_error(
            hooks, request.archive_path, state.archive_path_reported, resolution.error_message);
        if (state.emit_progress) {
            state.emit_progress();
        }
        return resolution;
    }

    STDMETHODIMP NativeExtractCallback::RequestMemoryUse(UInt32 flags,
                                                         UInt32 index_type,
                                                         UInt32,
                                                         wchar_t const* path,
                                                         UInt64 required_size,
                                                         UInt64* allowed_size,
                                                         UInt32* answer_flags) throw() {
        (void)index_type;
        if (allowed_size == nullptr || answer_flags == nullptr) {
            return E_INVALIDARG;
        }

        ExtractMemoryRequest request;
        request.flags = flags;
        request.required_size = required_size;
        request.allowed_size = *allowed_size;
        request.answer_flags = *answer_flags;
        request.configured_limit_bytes = configured_memory_limit_bytes_;
        request.configured_limit_defined = configured_memory_limit_defined_;
        request.archive_path = archive_path_;
        request.file_path = path == nullptr ? std::string() : ustring_to_utf8(UString(path));
        ExtractMemoryResolution const resolution = handle_extract_memory_request(
            request,
            hooks_,
            ExtractMemoryCallbackState{memory_skip_handled_,
                                       memory_error_reported_,
                                       archive_error_path_reported_,
                                       mutex_,
                                       diagnostic_message_,
                                       error_count_,
                                       [this]() { emit_progress_snapshot(); }});

        *allowed_size = resolution.allowed_size;
        *answer_flags = resolution.answer_flags;
        return resolution.hresult;
    }

} // namespace z7::app
