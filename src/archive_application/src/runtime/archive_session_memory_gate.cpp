// src/archive_application/src/runtime/archive_session_memory_gate.cpp
// Role: Benchmark memory gate helper for archive sessions.

#include "archive_session_helpers.h"

#include "ports/archive_backend_port.h"

namespace z7::app {
namespace {

OperationResult missing_memory_limit_delegate_result() {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = 7;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(
      ArchiveErrorDomain::kInvalidArguments,
      "Missing delegate interaction handler for benchmark memory limit prompt",
      result.native_exit_code);
  result.summary = describe_archive_error(result.error);
  return result;
}

}  // namespace
}  // namespace z7::app

namespace z7::app::archive_session_helpers {

std::optional<OperationResult> maybe_block_benchmark_for_memory_limit(
    const ArchiveRequest& request,
    IArchiveDelegate* delegate) {
  if (!std::holds_alternative<BenchmarkRequest>(request.payload)) {
    return std::nullopt;
  }

  const auto& benchmark = std::get<BenchmarkRequest>(request.payload);
  const BenchmarkMemoryEstimate estimate = estimate_benchmark_memory_native(benchmark);
  if (!estimate.ok || estimate.within_limit) {
    return std::nullopt;
  }

  MemoryLimitPrompt prompt;
  prompt.estimated_usage_bytes = estimate.estimated_usage_bytes;
  prompt.safe_limit_defined = estimate.ram_defined;
  prompt.safe_limit_bytes = estimate.safe_ram_limit_bytes;

  bool has_decision = false;
  MemoryLimitReply reply;
  if (delegate != nullptr) {
    if (const std::optional<MemoryLimitReply> maybe_reply =
            delegate->request_memory_limit(prompt);
        maybe_reply.has_value()) {
      reply = *maybe_reply;
      has_decision = true;
    }
  }

  if (!has_decision) {
    return missing_memory_limit_delegate_result();
  }

  const bool allow_once = reply.action == MemoryLimitAction::kAllowOnce;
  const bool allow_with_updated_limit =
      reply.action == MemoryLimitAction::kUpdateLimitAndContinue &&
      reply.updated_limit_bytes != 0 &&
      estimate.estimated_usage_bytes <= reply.updated_limit_bytes;
  if (allow_once || allow_with_updated_limit) {
    return std::nullopt;
  }

  OperationResult result;
  result.ok = false;
  result.native_exit_code = 255;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCanceled;
  result.error = make_archive_error(ArchiveErrorDomain::kCanceled,
                                    "Operation skipped due to memory limit policy.",
                                    result.native_exit_code);
  result.summary = describe_archive_error(result.error);
  return result;
}

}  // namespace z7::app::archive_session_helpers
