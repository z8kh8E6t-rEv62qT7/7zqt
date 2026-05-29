// src/archive_application/src/native_7z/callbacks/callbacks_extract_memory.cpp
// Role: Extract callback memory-limit interaction handling.

#include "core/internal.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_extract_run.h"

namespace z7::app {

STDMETHODIMP NativeExtractCallback::RequestMemoryUse(UInt32 flags,
                                                     UInt32 index_type,
                                                     UInt32,
                                                     const wchar_t* path,
                                                     UInt64 required_size,
                                                     UInt64* allowed_size,
                                                     UInt32* answer_flags) throw() {
  (void)index_type;
  if (allowed_size == nullptr || answer_flags == nullptr) {
    return E_INVALIDARG;
  }

  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
    if (configured_memory_limit_defined_) {
      *allowed_size = configured_memory_limit_bytes_;
    }
  }

  UInt64 current_limit = *allowed_size;
  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
    if (required_size <= current_limit) {
      *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
      return S_OK;
    }
    *answer_flags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
    if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) {
      *answer_flags |= NRequestMemoryAnswerFlags::k_SkipArc;
    }
  }

  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0 &&
      hooks_.ask_memory_limit) {
    MemoryLimitPrompt prompt;
    prompt.required_usage_bytes = required_size;
    prompt.current_limit_bytes = current_limit;
    prompt.current_limit_defined = current_limit != 0;
    prompt.archive_path = archive_path_;
    if (path != nullptr) {
      prompt.file_path = ustring_to_utf8(UString(path));
    }
    prompt.skip_archive_supported =
        (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) != 0;
    prompt.report_only =
        (flags & NRequestMemoryUseFlags::k_IsReport) != 0;

    const MemoryLimitReply reply = hooks_.ask_memory_limit(prompt);
    switch (reply.action) {
      case MemoryLimitAction::kAllowOnce:
        *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
        return S_OK;
      case MemoryLimitAction::kUpdateLimitAndContinue:
        if (reply.updated_limit_bytes != 0) {
          *allowed_size = reply.updated_limit_bytes;
          if (required_size <= reply.updated_limit_bytes) {
            *answer_flags = NRequestMemoryAnswerFlags::k_Allow;
            return S_OK;
          }
        }
        break;
      case MemoryLimitAction::kSkipOperation:
        if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) {
          *answer_flags = NRequestMemoryAnswerFlags::k_SkipArc |
                          NRequestMemoryAnswerFlags::k_Limit_Exceeded;
          return S_OK;
        }
        *answer_flags = NRequestMemoryAnswerFlags::k_Stop;
        return E_ABORT;
      case MemoryLimitAction::kCancelOperation:
        *answer_flags = NRequestMemoryAnswerFlags::k_Stop;
        return E_ABORT;
    }
  }

  if ((flags & NRequestMemoryUseFlags::k_NoErrorMessage) == 0) {
    const uint64_t required_mb = required_size >> 20;
    const uint64_t allowed_mb = current_limit >> 20;
    std::string message = "Memory usage limit exceeded";
    message += " (required " + std::to_string(required_mb) + " MB, allowed " +
               std::to_string(allowed_mb) + " MB)";
    if (!archive_path_.empty()) {
      message += " for " + archive_path_;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!diagnostic_message_.empty()) {
        diagnostic_message_ += '\n';
      }
      diagnostic_message_ += message;
      ++error_count_;
    }
    emit_log_event(hooks_,
                   OperationStage::kRunning,
                   OutputChannel::kStdErr,
                   message);
    emit_progress_snapshot();
  }

  return S_OK;
}

}  // namespace z7::app
