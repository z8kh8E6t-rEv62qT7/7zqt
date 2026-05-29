// src/archive_application/src/native_7z/backend/native_request_policy.h
// Role: Private ArchiveRequest operation names and OperationRunner policy.

#pragma once

#include <string_view>
#include <type_traits>

#include "backend/native_request_validation.h"
#include "backend/operation_runner.h"

namespace z7::app {

template <typename TRequest>
constexpr std::string_view request_operation_name() {
  if constexpr (std::is_same_v<TRequest, AddRequest>) {
    return "add";
  } else if constexpr (std::is_same_v<TRequest, ExtractRequest>) {
    return "extract";
  } else if constexpr (std::is_same_v<TRequest, TestRequest>) {
    return "test";
  } else if constexpr (std::is_same_v<TRequest, BenchmarkRequest>) {
    return "benchmark";
  } else if constexpr (std::is_same_v<TRequest, SplitRequest>) {
    return "split";
  } else if constexpr (std::is_same_v<TRequest, CombineRequest>) {
    return "combine";
  } else if constexpr (std::is_same_v<TRequest, HashRequest>) {
    return "hash";
  } else if constexpr (std::is_same_v<TRequest, DeleteRequest>) {
    return "delete";
  } else if constexpr (std::is_same_v<TRequest, OpenArchiveRequest>) {
    return "open_archive";
  } else if constexpr (std::is_same_v<TRequest, OpenArchiveFromPathRequest>) {
    return "open_archive_from_path";
  } else if constexpr (std::is_same_v<TRequest, OpenArchiveFromParentRequest>) {
    return "open_archive_from_parent";
  } else if constexpr (std::is_same_v<TRequest, CloseArchiveSessionRequest>) {
    return "close_archive_session";
  } else if constexpr (std::is_same_v<TRequest, ListRequest>) {
    return "list";
  } else if constexpr (std::is_same_v<TRequest, ArchivePropertiesRequest>) {
    return "properties";
  } else if constexpr (std::is_same_v<TRequest, NavigateRequest>) {
    return "navigate";
  } else if constexpr (std::is_same_v<TRequest, CopyRequest>) {
    return "copy";
  } else if constexpr (std::is_same_v<TRequest, MoveRequest>) {
    return "move";
  } else if constexpr (std::is_same_v<TRequest, RenameRequest>) {
    return "rename";
  } else if constexpr (std::is_same_v<TRequest, CreateRequest>) {
    return "create";
  } else if constexpr (std::is_same_v<TRequest, ArchiveCommentRequest>) {
    return "comment_archive";
  } else if constexpr (std::is_same_v<TRequest, FilesystemCommentRequest>) {
    return "comment_filesystem";
  } else if constexpr (std::is_same_v<TRequest, GetEntryInfoRequest>) {
    return "get_entry_info";
  } else {
    return "operation";
  }
}

template <typename TRequest, typename TResult>
typename OperationRunner<TRequest, TResult>::Options make_runner_options(
    const TRequest& request) {
  using Runner = OperationRunner<TRequest, TResult>;
  typename OperationRunner<TRequest, TResult>::Options options;
  options.validator = [](const TRequest& req) { return validate_request(req); };
  options.operation_name = request_operation_name<TRequest>();
  options.enforce_lifecycle = true;

  if constexpr (std::is_same_v<TRequest, DeleteRequest>) {
    options.require_codecs_when = [](const TRequest& req) {
      return req.filesystem_paths.empty();
    };
    options.execution_envelope_when = [](const TRequest& req) {
      return req.filesystem_paths.empty() ? Runner::ExecutionEnvelope::kPauseable
                                          : Runner::ExecutionEnvelope::kCancelable;
    };
    options.pauseable_flag = Runner::PauseableFlag::kUpdating;
  } else if constexpr (std::is_same_v<TRequest, RenameRequest>) {
    options.require_codecs_when = [](const TRequest& req) {
      return (req.session_token.has_value() && req.session_token->is_valid()) ||
             !req.archive_path.empty() || !req.entry_path.empty();
    };
    options.execution_envelope_when = [](const TRequest& req) {
      return ((req.session_token.has_value() && req.session_token->is_valid()) ||
              !req.archive_path.empty() || !req.entry_path.empty())
                 ? Runner::ExecutionEnvelope::kPauseable
                 : Runner::ExecutionEnvelope::kDirect;
    };
    options.pauseable_flag = Runner::PauseableFlag::kUpdating;
  } else if constexpr (std::is_same_v<TRequest, AddRequest> ||
                std::is_same_v<TRequest, ExtractRequest> ||
                std::is_same_v<TRequest, TestRequest> ||
                std::is_same_v<TRequest, BenchmarkRequest> ||
                std::is_same_v<TRequest, OpenArchiveRequest> ||
                std::is_same_v<TRequest, OpenArchiveFromPathRequest> ||
                std::is_same_v<TRequest, OpenArchiveFromParentRequest> ||
                std::is_same_v<TRequest, ListRequest> ||
                std::is_same_v<TRequest, ArchivePropertiesRequest> ||
                std::is_same_v<TRequest, ArchiveCommentRequest> ||
                std::is_same_v<TRequest, GetEntryInfoRequest>) {
    options.require_codecs = true;
  }

  if constexpr (std::is_same_v<TRequest, AddRequest> ||
                std::is_same_v<TRequest, ArchiveCommentRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kPauseable;
    options.pauseable_flag = Runner::PauseableFlag::kUpdating;
  } else if constexpr (std::is_same_v<TRequest, HashRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kPauseable;
    options.pauseable_flag = Runner::PauseableFlag::kHashing;
  } else if constexpr (std::is_same_v<TRequest, ExtractRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kPauseable;
    options.pauseable_flag = Runner::PauseableFlag::kExtracting;
  } else if constexpr (std::is_same_v<TRequest, TestRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kPauseable;
    options.pauseable_flag = Runner::PauseableFlag::kTesting;
  } else if constexpr (std::is_same_v<TRequest, BenchmarkRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kPauseable;
    options.pauseable_flag = Runner::PauseableFlag::kBenchmarking;
  } else if constexpr (std::is_same_v<TRequest, ListRequest> ||
                       std::is_same_v<TRequest, ArchivePropertiesRequest> ||
                       std::is_same_v<TRequest, OpenArchiveRequest> ||
                       std::is_same_v<TRequest, OpenArchiveFromPathRequest> ||
                       std::is_same_v<TRequest, OpenArchiveFromParentRequest> ||
                       std::is_same_v<TRequest, CloseArchiveSessionRequest> ||
                       std::is_same_v<TRequest, CopyRequest> ||
                       std::is_same_v<TRequest, MoveRequest> ||
                       std::is_same_v<TRequest, SplitRequest> ||
                       std::is_same_v<TRequest, CombineRequest> ||
                       std::is_same_v<TRequest, GetEntryInfoRequest> ||
                       std::is_same_v<TRequest, FilesystemCommentRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kCancelable;
  } else if constexpr (std::is_same_v<TRequest, NavigateRequest> ||
                       std::is_same_v<TRequest, CreateRequest>) {
    options.execution_envelope = Runner::ExecutionEnvelope::kDirect;
  }

  (void)request;
  return options;
}

}  // namespace z7::app
