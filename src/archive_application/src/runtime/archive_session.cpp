#include "archive_session.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "archive_session_core.h"
#include "ports/archive_backend_port.h"
#include "archive_error.h"

namespace z7::app {

namespace {

OperationResult build_backend_unavailable_result() {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = 2;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(ArchiveErrorDomain::kBackendUnavailable,
                                    "No archive backend available.",
                                    result.native_exit_code);
  result.summary = describe_archive_error(result.error);
  return result;
}

}  // namespace

ArchiveSession::ArchiveSession(std::shared_ptr<ArchiveSessionCore> session)
    : session_(std::move(session)) {}

bool ArchiveSession::valid() const {
  return session_ != nullptr;
}

void ArchiveSession::cancel() const {
  if (session_ != nullptr) {
    session_->cancel();
  }
}

void ArchiveSession::pause() const {
  if (session_ != nullptr) {
    session_->pause();
  }
}

void ArchiveSession::resume() const {
  if (session_ != nullptr) {
    session_->resume();
  }
}

ArchiveSessionState ArchiveSession::state() const {
  return session_ != nullptr ? session_->state() : ArchiveSessionState::kFailed;
}

ProgressSnapshot ArchiveSession::snapshot() const {
  return session_ != nullptr ? session_->snapshot() : ProgressSnapshot{};
}

ArchiveSession ArchiveEngine::start(const ArchiveRequest& request,
                                    std::shared_ptr<IArchiveDelegate> delegate) {
  std::unique_ptr<INativeArchiveBackend> backend = create_native_archive_backend();
  if (backend == nullptr) {
    return ArchiveSession();
  }

  auto session = std::make_shared<ArchiveSessionCore>(request, std::move(delegate),
                                                      std::move(backend));
  if (!session->start()) {
    return ArchiveSession();
  }
  return ArchiveSession(std::move(session));
}

BackendCapabilities ArchiveEngine::capabilities() const {
  return query_capabilities();
}

BackendCapabilities ArchiveEngine::query_capabilities() {
  std::unique_ptr<INativeArchiveBackend> backend = create_native_archive_backend();
  return backend != nullptr ? backend->capabilities() : BackendCapabilities{};
}

OperationResult operation_result_from_outcome(const OperationOutcome& outcome) {
  return std::visit(
      [&](const auto& value) -> OperationResult {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          OperationResult result;
          result.ok = outcome.ok;
          result.native_exit_code = outcome.native_code;
          result.native_execution = outcome.native_execution;
          result.error = outcome.error;
          result.summary = outcome.summary;
          return result;
        } else {
          return static_cast<OperationResult>(value);
        }
      },
      outcome.payload);
}

OperationResult make_immediate_result(int native_exit_code,
                                      ArchiveErrorDomain domain,
                                      std::string_view summary) {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = native_exit_code;
  result.native_execution.native_exit_code = native_exit_code;
  result.native_execution.termination_reason =
      domain == ArchiveErrorDomain::kCanceled
          ? NativeTerminationReason::kCanceled
          : NativeTerminationReason::kCompleted;
  result.error =
      make_archive_error(domain, std::string(summary), native_exit_code);
  result.summary = std::string(summary);
  return result;
}

OperationResult make_backend_unavailable_result() {
  return build_backend_unavailable_result();
}

OperationOutcome make_backend_unavailable_outcome() {
  const OperationResult result = build_backend_unavailable_result();

  OperationOutcome outcome;
  outcome.status = OperationStatus::kFailed;
  outcome.error_domain = ArchiveErrorDomain::kBackendUnavailable;
  outcome.native_code = result.native_exit_code;
  outcome.summary = result.summary;
  outcome.error = result.error;
  outcome.native_execution = result.native_execution;
  outcome.ok = result.ok;
  return outcome;
}

BenchmarkMemoryEstimate estimate_benchmark_memory(const BenchmarkRequest& request) {
  return estimate_benchmark_memory_native(request);
}

CompressionResourcesEstimate estimate_compression_resources(const AddRequest& request) {
  return estimate_compression_resources_native(request);
}

BenchmarkSystemInfo query_benchmark_system_info() {
  return query_benchmark_system_info_native();
}

}  // namespace z7::app
