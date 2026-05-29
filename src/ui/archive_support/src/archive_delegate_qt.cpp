#include "archive_delegate_qt.h"

#include <utility>

namespace z7::ui::archive_support {

CompletionRelayDelegate::CompletionRelayDelegate(FinishedCallback finished_cb)
    : finished_cb_(std::move(finished_cb)) {}

void CompletionRelayDelegate::on_finished(
    const z7::app::OperationOutcome& outcome) {
  if (!finished_cb_) {
    return;
  }
  finished_cb_(outcome);
}

OutcomeRelayDelegate::OutcomeRelayDelegate(
    QObject* primary_target,
    FinishedCallback finished_cb,
    QObject* fallback_target,
    MissingTargetPolicy missing_target_policy)
    : primary_target_(primary_target),
      fallback_target_(fallback_target),
      finished_cb_(std::move(finished_cb)),
      missing_target_policy_(missing_target_policy) {}

// See archive_delegate_qt.h for rationale; this is a deliberate channel
// merge, not a fallback. Subclasses with a structured event pipeline must
// override this to a no-op to preserve lifecycle/log channel separation.
void OutcomeRelayDelegate::on_lifecycle(
    z7::app::OperationStage stage,
    std::string_view message) {
  z7::app::ArchiveLog log;
  log.stage = stage;
  log.channel = z7::app::OutputChannel::kStdOut;
  log.message.assign(message);
  this->on_log(log);
}

void OutcomeRelayDelegate::on_finished(
    const z7::app::OperationOutcome& outcome) {
  if (!finished_cb_) {
    return;
  }

  const z7::app::OperationOutcome copied = outcome;
  if (QObject* target = dispatch_target()) {
    dispatch_queued(target, [finished_cb = finished_cb_, copied](QObject*) mutable {
      if (finished_cb) {
        finished_cb(copied);
      }
    });
    return;
  }

  if (missing_target_policy_ == MissingTargetPolicy::kInvokeDirect) {
    finished_cb_(copied);
  }
}

QObject* OutcomeRelayDelegate::dispatch_target() const {
  if (!primary_target_.isNull()) {
    return primary_target_.data();
  }
  if (!fallback_target_.isNull()) {
    return fallback_target_.data();
  }
  return nullptr;
}

}  // namespace z7::ui::archive_support
