#include "archive_session_interactions.h"

#include <utility>
#include <variant>

#include "archive_session_helpers.h"
#include "archive_error.h"

namespace z7::app {
namespace {

OperationResult build_missing_interaction_result(std::string_view method_name) {
  OperationResult result;
  result.ok = false;
  result.native_exit_code = 7;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
  result.error = make_archive_error(
      ArchiveErrorDomain::kInvalidArguments,
      "Missing delegate interaction handler: IArchiveDelegate::" +
          std::string(method_name),
      result.native_exit_code);
  result.summary = describe_archive_error(result.error);
  return result;
}

}  // namespace

ArchiveSessionInteractionBroker::ArchiveSessionInteractionBroker(
    std::shared_ptr<IArchiveDelegate> delegate)
    : delegate_(std::move(delegate)) {}

ArchiveBackendHooks ArchiveSessionInteractionBroker::make_hooks(
    const ArchiveRequest& request,
    NativeEventCallback on_event) {
  ArchiveBackendHooks callbacks;
  callbacks.ask_overwrite = [this](const OverwritePrompt& prompt) {
    return request_overwrite(prompt);
  };
  callbacks.ask_password = [this](const PasswordPrompt& prompt) {
    return request_password(prompt);
  };
  callbacks.ask_choice = [this](const ChoicePrompt& prompt) {
    return request_choice(prompt);
  };
  callbacks.ask_memory_limit = [this](const MemoryLimitPrompt& prompt) {
    return request_memory_limit(prompt);
  };
  callbacks.on_event = std::move(on_event);
  if (const auto* list_request = std::get_if<ListRequest>(&request.payload);
      list_request != nullptr && list_request->streaming_mode) {
    callbacks.on_list_batch = [this](std::vector<ArchiveListEntry>&& batch) {
      return forward_list_batch(std::move(batch));
    };
  }
  return callbacks;
}

std::optional<OperationResult>
ArchiveSessionInteractionBroker::missing_interaction_result() const {
  return missing_interaction_;
}

void ArchiveSessionInteractionBroker::mark_missing_interaction(
    std::string_view method_name) {
  if (missing_interaction_.has_value()) {
    return;
  }
  missing_interaction_ = build_missing_interaction_result(method_name);
}

OverwriteDecision ArchiveSessionInteractionBroker::request_overwrite(
    const OverwritePrompt& prompt) {
  if (!delegate_) {
    mark_missing_interaction("request_overwrite");
    return OverwriteDecision::kCancel;
  }
  const std::optional<OverwriteDecision> reply =
      delegate_->request_overwrite(prompt);
  if (!reply.has_value()) {
    mark_missing_interaction("request_overwrite");
    return OverwriteDecision::kCancel;
  }
  return *reply;
}

PasswordReply ArchiveSessionInteractionBroker::request_password(
    const PasswordPrompt& prompt) {
  const bool wrong_password =
      archive_session_helpers::is_wrong_password_prompt(prompt);
  if (wrong_password) {
    std::lock_guard<std::mutex> lock(password_cache_mutex_);
    cached_password_.reset();
  } else {
    std::lock_guard<std::mutex> lock(password_cache_mutex_);
    if (cached_password_.has_value() && !cached_password_->empty()) {
      PasswordReply cached_reply;
      cached_reply.kind = PasswordReplyKind::kProvide;
      cached_reply.password = *cached_password_;
      return cached_reply;
    }
  }

  PasswordReply reply;
  reply.kind = PasswordReplyKind::kCancel;
  if (!delegate_) {
    mark_missing_interaction("request_password");
    return reply;
  }

  const std::optional<PasswordReply> maybe_reply =
      delegate_->request_password(prompt);
  if (!maybe_reply.has_value()) {
    mark_missing_interaction("request_password");
    return reply;
  }

  reply = *maybe_reply;
  if (reply.kind == PasswordReplyKind::kProvide && !reply.password.empty()) {
    std::lock_guard<std::mutex> lock(password_cache_mutex_);
    cached_password_ = reply.password;
  }
  return reply;
}

ChoiceReply ArchiveSessionInteractionBroker::request_choice(
    const ChoicePrompt& prompt) {
  if (!delegate_) {
    mark_missing_interaction("request_choice");
    return ChoiceReply{};
  }
  const std::optional<ChoiceReply> reply = delegate_->request_choice(prompt);
  if (!reply.has_value()) {
    mark_missing_interaction("request_choice");
    return ChoiceReply{};
  }
  return *reply;
}

MemoryLimitReply ArchiveSessionInteractionBroker::request_memory_limit(
    const MemoryLimitPrompt& prompt) {
  MemoryLimitReply fallback;
  fallback.action = MemoryLimitAction::kSkipOperation;
  if (!delegate_) {
    mark_missing_interaction("request_memory_limit");
    return fallback;
  }
  const std::optional<MemoryLimitReply> reply =
      delegate_->request_memory_limit(prompt);
  if (!reply.has_value()) {
    mark_missing_interaction("request_memory_limit");
    return fallback;
  }
  return *reply;
}

bool ArchiveSessionInteractionBroker::forward_list_batch(
    std::vector<ArchiveListEntry>&& batch) {
  if (!delegate_) {
    return true;
  }
  return delegate_->on_list_entries_batch(std::move(batch));
}

}  // namespace z7::app
