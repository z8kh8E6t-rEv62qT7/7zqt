#include "archive_session_helpers.h"

#include <utility>

#include "archive_error.h"

namespace z7::app {
namespace {

bool is_benchmark_request(const ArchiveRequest& request) {
  return std::holds_alternative<BenchmarkRequest>(request.payload);
}

bool is_wrong_password_prompt(const PasswordPrompt& prompt) {
  return prompt.reason_kind == PasswordPromptReason::kWrongPassword;
}

OperationStatus status_from_result(const OperationResult& result) {
  if (is_operation_canceled(result.error)) {
    return OperationStatus::kCanceled;
  }
  if (result.error.domain == ArchiveErrorDomain::kPartialSuccess) {
    return OperationStatus::kPartialSuccess;
  }
  if (result.error.domain == ArchiveErrorDomain::kPassword) {
    return OperationStatus::kWrongPassword;
  }
  if (result.ok && result.error.domain == ArchiveErrorDomain::kNone) {
    return OperationStatus::kSuccess;
  }
  return OperationStatus::kFailed;
}

OperationOutcome make_outcome(const OperationResult& base, OperationPayload payload) {
  OperationOutcome out;
  out.status = status_from_result(base);
  out.error_domain = base.error.domain;
  out.native_code = base.native_exit_code;
  out.summary = base.summary;
  out.error = base.error;
  out.native_execution = base.native_execution;
  out.ok = base.ok;
  out.payload = std::move(payload);
  if (!base.error.message.empty()) {
    out.diagnostics.push_back(base.error.message);
  }
  if (!base.summary.empty() && (base.error.message.empty() || base.summary != base.error.message)) {
    out.diagnostics.push_back(base.summary);
  }
  return out;
}

}  // namespace

namespace archive_session_helpers {

bool is_benchmark_request(const ArchiveRequest& request) {
  return z7::app::is_benchmark_request(request);
}

bool is_wrong_password_prompt(const PasswordPrompt& prompt) {
  return z7::app::is_wrong_password_prompt(prompt);
}

OperationOutcome make_outcome(const OperationResult& base, OperationPayload payload) {
  return z7::app::make_outcome(base, std::move(payload));
}

}  // namespace archive_session_helpers

}  // namespace z7::app
