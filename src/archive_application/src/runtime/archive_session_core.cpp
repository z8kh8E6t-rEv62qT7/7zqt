#include "archive_session_core.h"

#include <optional>
#include <utility>
#include <variant>

#include "archive_session_helpers.h"

namespace z7::app {

using archive_session_helpers::make_outcome;
using archive_session_helpers::maybe_block_benchmark_for_memory_limit;

ArchiveSessionCore::ArchiveSessionCore(
    const ArchiveRequest& request,
    std::shared_ptr<IArchiveDelegate> delegate,
    std::unique_ptr<INativeArchiveBackend> backend)
    : request_(request),
      delegate_(std::move(delegate)),
      backend_(std::move(backend)),
      event_sink_(delegate_,
                  request_,
                  [this](ArchiveSessionState state) { set_state(state); }),
      interaction_broker_(delegate_) {}

ArchiveSessionCore::~ArchiveSessionCore() {
  if (!worker_.joinable()) {
    return;
  }
  if (worker_.get_id() == std::this_thread::get_id()) {
    // The worker lambda can hold the final shared_ptr; avoid self-joining.
    worker_.detach();
    return;
  }
  worker_.join();
}

bool ArchiveSessionCore::start() {
  if (backend_ == nullptr) {
    return false;
  }
  worker_ = std::thread([self = shared_from_this()]() { self->run_worker(); });
  return true;
}

void ArchiveSessionCore::cancel() {
  set_state(ArchiveSessionState::kCancelling);
  if (backend_ != nullptr) {
    backend_->cancel();
  }
}

void ArchiveSessionCore::pause() {
  if (backend_ == nullptr || !backend_->supports_pause()) {
    return;
  }
  backend_->pause();
  set_state(ArchiveSessionState::kPaused);
}

void ArchiveSessionCore::resume() {
  if (backend_ == nullptr || !backend_->supports_pause()) {
    return;
  }
  backend_->resume();
  set_state(ArchiveSessionState::kRunning);
}

ArchiveSessionState ArchiveSessionCore::state() const {
  return state_.load();
}

ProgressSnapshot ArchiveSessionCore::snapshot() const {
  return event_sink_.snapshot();
}

void ArchiveSessionCore::run_worker() {
  set_state(ArchiveSessionState::kRunning);
  event_sink_.reset_diagnostics();

  ArchiveBackendHooks callbacks = interaction_broker_.make_hooks(
      request_,
      [this](const OperationEvent& event) { event_sink_.on_event(event); });

  NativeInvokeResult invoke_result;
  if (backend_ != nullptr) {
    if (const std::optional<OperationResult> blocked =
            maybe_block_benchmark_for_memory_limit(request_, delegate_.get())) {
      invoke_result.base = *blocked;
      invoke_result.payload = std::monostate{};
    } else {
      invoke_result = backend_->invoke(request_, callbacks);
      if (const std::optional<OperationResult> missing_interaction =
              interaction_broker_.missing_interaction_result()) {
        invoke_result.base = *missing_interaction;
        invoke_result.payload = std::monostate{};
      }
    }
  } else {
    invoke_result.base = make_backend_unavailable_result();
    invoke_result.payload = std::monostate{};
  }

  OperationOutcome outcome =
      make_outcome(invoke_result.base, std::move(invoke_result.payload));
  event_sink_.merge_diagnostics(outcome);

  switch (outcome.status) {
    case OperationStatus::kSuccess:
    case OperationStatus::kPartialSuccess:
      set_state(ArchiveSessionState::kCompleted);
      break;
    case OperationStatus::kCanceled:
      set_state(ArchiveSessionState::kCancelled);
      break;
    case OperationStatus::kWrongPassword:
    case OperationStatus::kFailed:
      set_state(ArchiveSessionState::kFailed);
      break;
  }

  if (delegate_) {
    delegate_->on_finished(outcome);
  }

  (void)outcome;
}

void ArchiveSessionCore::set_state(ArchiveSessionState expected) {
  ArchiveSessionState current = state_.load();
  if (current == ArchiveSessionState::kCompleted ||
      current == ArchiveSessionState::kFailed ||
      current == ArchiveSessionState::kCancelled) {
    return;
  }
  state_.store(expected);
}

}  // namespace z7::app
