#pragma once

#include <functional>
#include <string_view>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

#include "archive_session.h"

namespace z7::ui::archive_support {

enum class MissingTargetPolicy {
  kDrop,
  kInvokeDirect,
};

template <class TResult, class Prompt, class Handler>
TResult call_on_target_blocking(QObject* target,
                                const Prompt& prompt,
                                TResult default_result,
                                Handler&& handler) {
  QPointer<QObject> guarded_target(target);
  if (guarded_target.isNull()) {
    return default_result;
  }
  if (QThread::currentThread() == guarded_target->thread()) {
    return handler(prompt);
  }

  TResult result = std::move(default_result);
  QMetaObject::invokeMethod(
      guarded_target.data(),
      [guarded_target, &prompt, &result, handler = std::forward<Handler>(handler)]() mutable {
        if (guarded_target.isNull()) {
          return;
        }
        result = handler(prompt);
      },
      Qt::BlockingQueuedConnection);
  return result;
}

class CompletionRelayDelegate final : public z7::app::IArchiveDelegate {
 public:
  using FinishedCallback =
      std::function<void(const z7::app::OperationOutcome&)>;

  explicit CompletionRelayDelegate(FinishedCallback finished_cb);

  void on_finished(const z7::app::OperationOutcome& outcome) override;

 private:
  FinishedCallback finished_cb_;
};

class OutcomeRelayDelegate : public z7::app::IArchiveDelegate {
 public:
  using FinishedCallback =
      std::function<void(const z7::app::OperationOutcome&)>;

  OutcomeRelayDelegate(QObject* primary_target,
                       FinishedCallback finished_cb,
                       QObject* fallback_target = nullptr,
                       MissingTargetPolicy missing_target_policy =
                           MissingTargetPolicy::kInvokeDirect);

  // Deliberate default: synthesize an ArchiveLog from `(stage, message)`
  // (channel = kStdOut) and dispatch it through `on_log(log)`. This exists so
  // simple UI-dialog subclasses that only override `on_log` still receive the
  // pure stage-transition events the backend synthesizes in
  // `operation_runner.h::emit_stage_if_missing` (which carry a stage but no
  // message payload). `ModalTaskDelegate` (gui_task_runner.cpp) and
  // `RunnerDelegate` (archive_process_runner/core.cpp) rely on this merge to
  // drive `dialog->set_stage(...)` / `stage_changed(...)` from lifecycle
  // events without having to override `on_lifecycle` themselves.
  //
  // Subclasses that run a structured event pipeline discriminating on
  // `OperationEventKind` (e.g. `BenchmarkOperationDelegate` in
  // ui/gui/src/benchmark/layout.cpp) MUST override `on_lifecycle` to a no-op
  // to preserve the lifecycle/log channel separation. That override is a
  // contract, not a legacy compatibility shim.
  void on_lifecycle(z7::app::OperationStage stage,
                    std::string_view message) override;
  void on_finished(const z7::app::OperationOutcome& outcome) override;

 protected:
  template <typename TObject, typename Fn>
  static void dispatch_queued(TObject* target, Fn&& fn) {
    QPointer<TObject> guarded_target(target);
    if (guarded_target.isNull()) {
      return;
    }
    QMetaObject::invokeMethod(
        guarded_target.data(),
        [guarded_target, fn = std::forward<Fn>(fn)]() mutable {
          if (guarded_target.isNull()) {
            return;
          }
          fn(guarded_target.data());
        },
        Qt::QueuedConnection);
  }

 private:
  QObject* dispatch_target() const;

  QPointer<QObject> primary_target_;
  QPointer<QObject> fallback_target_;
  FinishedCallback finished_cb_;
  MissingTargetPolicy missing_target_policy_ = MissingTargetPolicy::kInvokeDirect;
};

template <class TOwner>
class OwnerRelayDelegate : public OutcomeRelayDelegate {
 protected:
  using FinishedCallback = typename OutcomeRelayDelegate::FinishedCallback;

  OwnerRelayDelegate(TOwner* owner,
                     FinishedCallback finished_cb,
                     QObject* fallback_target = nullptr,
                     MissingTargetPolicy missing_target_policy =
                         MissingTargetPolicy::kInvokeDirect)
      : OutcomeRelayDelegate(owner,
                             std::move(finished_cb),
                             fallback_target,
                             missing_target_policy),
        owner_(owner) {}

  TOwner* owner() const {
    return owner_.data();
  }

  template <class Fn>
  void post_to_owner(Fn&& fn) {
    dispatch_queued(owner_.data(), std::forward<Fn>(fn));
  }

 private:
  QPointer<TOwner> owner_;
};

}  // namespace z7::ui::archive_support
