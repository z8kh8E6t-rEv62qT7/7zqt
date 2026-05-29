#pragma once

#include <QObject>
#include <QSharedPointer>

#include <atomic>

namespace z7::ui::gui {

class TaskCancellation final : public QObject {
  Q_OBJECT

 public:
  using Ptr = QSharedPointer<TaskCancellation>;

  static Ptr create() {
    return Ptr(new TaskCancellation(), &TaskCancellation::dispose);
  }

  bool is_canceled() const {
    return canceled_.load(std::memory_order_acquire);
  }

  void request_cancel() {
    bool expected = false;
    if (!canceled_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
      return;
    }
    emit cancel_requested();
  }

 signals:
  void cancel_requested();

 private:
  TaskCancellation() = default;
  static void dispose(TaskCancellation* cancellation) {
    if (cancellation != nullptr) {
      cancellation->deleteLater();
    }
  }

  std::atomic_bool canceled_{false};
};

using SharedTaskCancellation = TaskCancellation::Ptr;

}  // namespace z7::ui::gui
