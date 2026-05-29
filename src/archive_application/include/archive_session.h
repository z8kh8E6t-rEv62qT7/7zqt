#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "archive_types.h"

namespace z7::app {

enum class OperationStatus {
  kSuccess,
  kFailed,
  kCanceled,
  kPartialSuccess,
  kWrongPassword
};

enum class ArchiveSessionState {
  kPending,
  kRunning,
  kPaused,
  kCancelling,
  kCompleted,
  kFailed,
  kCancelled
};

struct ArchiveLog {
  OperationStage stage = OperationStage::kRunning;
  OutputChannel channel = OutputChannel::kNone;
  std::string message;
  std::optional<BenchmarkTypedSnapshot> benchmark_snapshot;
  std::optional<BenchmarkTypedSummary> benchmark_summary;
};

struct ProgressSnapshot {
  OperationStage stage = OperationStage::kRunning;
  int percent = -1;
  bool totals_known = false;
  uint64_t total_bytes = 0;
  uint64_t completed_bytes = 0;
  uint64_t total_files = 0;
  uint64_t completed_files = 0;
  uint64_t error_count = 0;
  std::string current_path;
  std::string message;
  std::optional<ProgressRatioInfo> ratio_info;
  std::optional<BenchmarkTypedSnapshot> benchmark_snapshot;
  std::optional<BenchmarkTypedSummary> benchmark_summary;
};

enum class PasswordPromptReason {
  kPasswordRequired,
  kWrongPassword
};

struct PasswordPrompt {
  std::string archive_path;
  // Stable diagnostic key for non-UI consumers. User-visible text must be
  // derived from reason_kind and localized at the UI boundary.
  std::string reason;
  PasswordPromptReason reason_kind = PasswordPromptReason::kPasswordRequired;
};

enum class PasswordReplyKind {
  kProvide,
  kCancel
};

struct PasswordReply {
  PasswordReplyKind kind = PasswordReplyKind::kCancel;
  std::string password;
};

struct ChoicePrompt {
  std::string title;
  std::string message;
  std::vector<std::string> choices;
  int default_index = 0;
};

enum class ChoiceReplyKind {
  kSelect,
  kCancel
};

struct ChoiceReply {
  ChoiceReplyKind kind = ChoiceReplyKind::kCancel;
  int selected_index = -1;
};

struct MemoryLimitPrompt {
  // Generic estimated usage (benchmark path).
  uint64_t estimated_usage_bytes = 0;
  // Generic safe limit (benchmark path).
  uint64_t safe_limit_bytes = 0;
  bool safe_limit_defined = false;
  // Required usage for current extraction step.
  uint64_t required_usage_bytes = 0;
  // Current configured allowed limit for extraction.
  uint64_t current_limit_bytes = 0;
  bool current_limit_defined = false;
  std::string archive_path;
  std::string file_path;
  bool test_mode = false;
  bool skip_archive_supported = false;
  bool report_only = false;
};

enum class MemoryLimitAction {
  kAllowOnce,
  kSkipOperation,
  kUpdateLimitAndContinue,
  kCancelOperation
};

struct MemoryLimitReply {
  MemoryLimitAction action = MemoryLimitAction::kAllowOnce;
  uint64_t updated_limit_bytes = 0;
};

using ArchiveRequestPayload =
    std::variant<AddRequest,
                 ExtractRequest,
                 TestRequest,
                 BenchmarkRequest,
                 SplitRequest,
                 CombineRequest,
                 HashRequest,
                 DeleteRequest,
                 OpenArchiveRequest,
                 OpenArchiveFromPathRequest,
                 OpenArchiveFromParentRequest,
                 CloseArchiveSessionRequest,
                 ListRequest,
                 ArchivePropertiesRequest,
                 NavigateRequest,
                 CopyRequest,
                 MoveRequest,
                 RenameRequest,
                 CreateRequest,
                 ArchiveCommentRequest,
                 FilesystemCommentRequest,
                 GetEntryInfoRequest>;

struct ArchiveRequest {
  ArchiveRequestPayload payload;
};

using OperationPayload =
    std::variant<std::monostate,
                 AddResult,
                 ExtractResult,
                 TestResult,
                 BenchmarkResult,
                 SplitResult,
                 CombineResult,
                 HashResult,
                 DeleteResult,
                 OpenArchiveResult,
                 OpenArchiveSessionResult,
                 ListResult,
                 ArchivePropertiesResult,
                 NavigateResult,
                 CopyResult,
                 MoveResult,
                 RenameResult,
                 CreateResult,
                 ArchiveCommentResult,
                 FilesystemCommentResult,
                 GetEntryInfoResult>;

struct OperationOutcome {
  OperationStatus status = OperationStatus::kFailed;
  ArchiveErrorDomain error_domain = ArchiveErrorDomain::kUnknown;
  int native_code = 0;
  std::string summary;
  ArchiveError error;
  NativeExecutionInfo native_execution;
  bool ok = false;
  OperationPayload payload;
  std::vector<std::string> diagnostics;
};

class IArchiveDelegate {
 public:
  virtual ~IArchiveDelegate() = default;
  virtual std::optional<OverwriteDecision> request_overwrite(
      const OverwritePrompt& prompt) {
    (void)prompt;
    return std::nullopt;
  }
  virtual std::optional<PasswordReply> request_password(
      const PasswordPrompt& prompt) {
    (void)prompt;
    return std::nullopt;
  }
  virtual std::optional<ChoiceReply> request_choice(const ChoicePrompt& prompt) {
    (void)prompt;
    return std::nullopt;
  }
  virtual std::optional<MemoryLimitReply> request_memory_limit(
      const MemoryLimitPrompt& prompt) {
    (void)prompt;
    return std::nullopt;
  }
  virtual void on_lifecycle(OperationStage stage, std::string_view message) {
    (void)stage;
    (void)message;
  }
  virtual void on_log(const ArchiveLog& log) {
    (void)log;
  }
  virtual void on_progress(const ProgressSnapshot& progress) {
    (void)progress;
  }
  // Streaming list callback — called on the backend worker thread each time a
  // batch of entries is ready (only when ListRequest::streaming_mode == true).
  // Return false to request early termination; backend will stop at the next
  // batch boundary and report OperationStatus::kCanceled.
  // When not overridden the default implementation accepts and discards the
  // batch, keeping the old behaviour intact for callers that use streaming_mode
  // only for its lower memory-peak property but do not need early termination.
  // NOTE: called on the backend worker thread — do not call UI from this method.
  virtual bool on_list_entries_batch(std::vector<ArchiveListEntry>&& batch) {
    (void)batch;
    return true;
  }
  virtual void on_finished(const OperationOutcome& outcome) {
    (void)outcome;
  }
};

class ArchiveSessionCore;

class ArchiveSession {
 public:
  ArchiveSession() = default;

  bool valid() const;
  void cancel() const;
  void pause() const;
  void resume() const;
  ArchiveSessionState state() const;
  ProgressSnapshot snapshot() const;

 private:
  friend class ArchiveEngine;
  explicit ArchiveSession(std::shared_ptr<ArchiveSessionCore> session);

  std::shared_ptr<ArchiveSessionCore> session_;
};

class ArchiveEngine {
 public:
  ArchiveSession start(const ArchiveRequest& request,
                       std::shared_ptr<IArchiveDelegate> delegate = {});

  BackendCapabilities capabilities() const;
  static BackendCapabilities query_capabilities();
};

OperationResult operation_result_from_outcome(const OperationOutcome& outcome);
OperationResult make_immediate_result(int native_exit_code,
                                      ArchiveErrorDomain domain,
                                      std::string_view summary);
OperationResult make_backend_unavailable_result();
OperationOutcome make_backend_unavailable_outcome();
BenchmarkMemoryEstimate estimate_benchmark_memory(const BenchmarkRequest& request);
CompressionResourcesEstimate estimate_compression_resources(const AddRequest& request);
BenchmarkSystemInfo query_benchmark_system_info();

template <typename TResult>
std::optional<TResult> outcome_payload_as(const OperationOutcome& outcome) {
  if (const auto* value = std::get_if<TResult>(&outcome.payload)) {
    return *value;
  }
  return std::nullopt;
}

}  // namespace z7::app
